/**
 * @file   z80core.c
 * @brief  Z80 CPU core: state, pins and the edge-stepped engine
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
 *
 * This file is part of Z80core, released under the terms of the GNU General
 * Public License version 2. See LICENSE.md for the full text.
 *
 * The engine is a cursor walking a list of per-edge steps. Every instruction
 * begins with the same M1 fetch; its last edge decodes the byte that arrived
 * and selects what runs next. Adding an instruction means writing its steps
 * and pointing the decoder at them - the engine itself never grows.
 *
 * PHASE 1: the register file, the fetch and the decode are real. The
 * instruction set is not: NOP and HALT execute, and anything else costs its
 * fetch and increments a counter (z80_unimplemented) so the gap is visible.
 */

#include "z80core.h"

#include <stdlib.h>
#include <string.h>

#define Z80CORE_VERSION "0.2.0-phase1"

/** Snapshot format, so a stale file is refused rather than misread. */
#define Z80_SNAPSHOT_MAGIC 0x5A383043u /* "Z80C" */
#define Z80_SNAPSHOT_VERSION 1u

/** Outputs the core drives; the rest of ctrl belongs to the host. */
#define Z80_OUTPUT_PINS (Z80_M1 | Z80_MREQ | Z80_IORQ | Z80_RD | Z80_WR | Z80_RFSH | Z80_HALT | Z80_BUSAK)

/** A register pair, addressable as a word or as its two halves. */
typedef union
{
    uint16_t word;
    struct
    {
        /* The Z80 is little-endian: C is the low half of BC, and so on. This
           core stores the halves explicitly rather than relying on the host's
           byte order, so it behaves identically on a big-endian machine. */
        uint8_t low;
        uint8_t high;
    } byte;
} z80_pair;

struct z80_t
{
    /* the register file */
    z80_pair af, bc, de, hl;
    z80_pair af_alt, bc_alt, de_alt, hl_alt;
    z80_pair ix, iy, sp, pc, wz;
    uint8_t i, r;

    /* interrupt state */
    uint8_t im;
    bool iff1, iff2;
    bool halted;

    /* the engine */
    const struct z80_seq *seq; /**< sequence being executed */
    uint8_t step;              /**< cursor into it */
    uint8_t opcode;            /**< what the last fetch latched */
    uint8_t clk;               /**< the level the core last advanced to */
    uint64_t edges;
    uint64_t unimplemented;
};

/**
 * A step runs on one clock edge and may drive pins. Steps never touch the
 * cursor; the engine owns it.
 */
typedef void (*z80_step_fn)(z80_t *cpu, z80_pins_t *pins);

/** A machine cycle, or a whole instruction, as a list of per-edge steps. */
typedef struct z80_seq
{
    const z80_step_fn *steps;
    uint8_t count;
} z80_seq;

static const z80_seq m1_fetch_seq;

/* ---------------------------------------------------------------- */
/* M1: the opcode fetch every instruction starts with                */
/* ---------------------------------------------------------------- */

static void step_nothing(z80_t *cpu, z80_pins_t *pins)
{
    (void)cpu;
    (void)pins;
}

static void step_m1_t1_rise(z80_t *cpu, z80_pins_t *pins)
{
    pins->A = cpu->pc.word;
    pins->ctrl |= Z80_M1;
}

static void step_m1_t1_fall(z80_t *cpu, z80_pins_t *pins)
{
    (void)cpu;
    pins->ctrl |= Z80_MREQ | Z80_RD;
}

static void step_m1_t3_rise(z80_t *cpu, z80_pins_t *pins)
{
    /* the opcode is sampled here, and the fetch ends */
    cpu->opcode = pins->D;

    /* A halted CPU keeps fetching from the halt address: it executes NOPs in
       place until something interrupts it, which is what the real part does. */
    if (!cpu->halted)
    {
        cpu->pc.word = (uint16_t)(cpu->pc.word + 1u);
    }

    pins->ctrl &= ~(uint32_t)(Z80_M1 | Z80_MREQ | Z80_RD);

    /* refresh address out, RFSH asserted, R incremented - bit 7 is not part
       of the counter and survives untouched */
    cpu->r = (uint8_t)((cpu->r & 0x80u) | ((cpu->r + 1u) & 0x7Fu));
    pins->A = (uint16_t)(((uint16_t)cpu->i << 8) | cpu->r);
    pins->ctrl |= Z80_RFSH;
}

static void step_m1_t3_fall(z80_t *cpu, z80_pins_t *pins)
{
    (void)cpu;
    /* the refresh is a real memory cycle: MREQ goes active again for it */
    pins->ctrl |= Z80_MREQ;
}

static void step_m1_t4_fall(z80_t *cpu, z80_pins_t *pins)
{
    (void)cpu;
    pins->ctrl &= ~(uint32_t)(Z80_MREQ | Z80_RFSH);
}

/**
 * One M1 opcode fetch, edge by edge (spec section 5.3), checked against the
 * A-Z80 timing table:
 *
 *     T1  M1                 T3  RFSH
 *     T2  M1 MREQ RD         T4  RFSH MREQ
 */
static const z80_step_fn m1_fetch_steps[] = {
    step_m1_t1_rise, /* T1 rise: address out, M1 asserted            */
    step_m1_t1_fall, /* T1 fall: MREQ, RD asserted                   */
    step_nothing,    /* T2 rise                                      */
    step_nothing,    /* T2 fall: WAIT is sampled here                */
    step_m1_t3_rise, /* T3 rise: opcode latched, fetch ends, refresh */
    step_m1_t3_fall, /* T3 fall: MREQ for the refresh cycle          */
    step_nothing,    /* T4 rise: decode                              */
    step_m1_t4_fall  /* T4 fall: MREQ and RFSH released              */
};

static const z80_seq m1_fetch_seq = {m1_fetch_steps, (uint8_t)(sizeof m1_fetch_steps / sizeof m1_fetch_steps[0])};

/* ---------------------------------------------------------------- */
/* Instructions                                                      */
/* ---------------------------------------------------------------- */

/**
 * HALT stops the program counter, not the clock. The core keeps running M1
 * cycles at the halt address - refreshing memory as it goes - with the HALT
 * pin asserted, until an interrupt or a reset releases it.
 */
static void step_halt(z80_t *cpu, z80_pins_t *pins)
{
    cpu->halted = true;
    pins->ctrl |= Z80_HALT;
}

static const z80_step_fn halt_steps[] = {step_halt};
static const z80_seq halt_seq = {halt_steps, 1};

/** An opcode the core does not implement yet: costs its fetch, does nothing. */
static void step_unimplemented(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    ++cpu->unimplemented;
}

static const z80_step_fn unimplemented_steps[] = {step_unimplemented};
static const z80_seq unimplemented_seq = {unimplemented_steps, 1};

/**
 * @brief Pick what runs after a fetch.
 *
 * Phase 1 knows two instructions. The shape is what matters: one lookup from
 * the latched opcode to a sequence, with the engine unchanged as the table
 * fills in.
 */
static const z80_seq *decode(const z80_t *cpu)
{
    switch (cpu->opcode)
    {
    case 0x00: /* NOP: the fetch was the whole instruction */
        return NULL;
    case 0x76: /* HALT */
        return &halt_seq;
    default:
        return &unimplemented_seq;
    }
}

/* ---------------------------------------------------------------- */
/* The engine                                                        */
/* ---------------------------------------------------------------- */

const char *z80_version(void)
{
    return Z80CORE_VERSION;
}

z80_t *z80_new(void)
{
    z80_t *cpu = calloc(1, sizeof *cpu);
    if (cpu)
    {
        z80_reset(cpu);
    }
    return cpu;
}

void z80_free(z80_t *cpu)
{
    free(cpu);
}

void z80_reset(z80_t *cpu)
{
    if (!cpu)
    {
        return;
    }
    memset(cpu, 0, sizeof *cpu);

    /* Spec 7.1: reset clears PC, I and R, disables interrupts and selects
       mode 0. AF and SP are left at FFFF, which is what the hardware powers
       up with and what every other emulator assumes. */
    cpu->af.word = 0xFFFF;
    cpu->sp.word = 0xFFFF;
    cpu->seq = &m1_fetch_seq;
}

/**
 * @brief Advance one edge and report what moved.
 *
 * The mask comes from comparing the pins before and after rather than from
 * each step announcing itself: a step that writes the value already there has
 * changed nothing, and the host should not be told otherwise.
 */
static uint32_t advance(z80_t *cpu, z80_pins_t *pins)
{
    const uint16_t address_before = pins->A;
    const uint8_t data_before = pins->D;
    const uint32_t ctrl_before = pins->ctrl;

    cpu->seq->steps[cpu->step](cpu, pins);
    ++cpu->edges;
    ++cpu->step;

    if (cpu->step >= cpu->seq->count)
    {
        /* the sequence is finished: run the decoded instruction if the fetch
           just ended, otherwise start the next fetch */
        const z80_seq *next = (cpu->seq == &m1_fetch_seq) ? decode(cpu) : NULL;
        cpu->seq = next ? next : &m1_fetch_seq;
        cpu->step = 0;
    }

    uint32_t changed = (pins->ctrl ^ ctrl_before) & Z80_OUTPUT_PINS;
    if (pins->A != address_before)
    {
        changed |= Z80_CHANGED_A;
    }
    if (pins->D != data_before)
    {
        changed |= Z80_CHANGED_D;
    }
    return changed;
}

uint32_t z80_tick(z80_t *cpu, z80_pins_t *pins, int clk)
{
    const uint8_t level = clk ? 1u : 0u;

    /* A clock that has not moved does not move the CPU. This is what lets a
       host gate, halt or hand-step the clock net (spec 4.2). */
    if (level == cpu->clk)
    {
        return 0;
    }
    cpu->clk = level;

    return advance(cpu, pins);
}

uint64_t z80_run(z80_t *cpu, z80_pins_t *pins, uint64_t edges)
{
    for (uint64_t i = 0; i < edges; ++i)
    {
        cpu->clk ^= 1u;
        (void)advance(cpu, pins);
    }
    return edges;
}

uint64_t z80_edges(const z80_t *cpu)
{
    return cpu->edges;
}

uint64_t z80_unimplemented(const z80_t *cpu)
{
    return cpu->unimplemented;
}

/* ---------------------------------------------------------------- */
/* Debug interface                                                   */
/* ---------------------------------------------------------------- */

/** The register pairs in the order z80_reg names them. */
static z80_pair *pair_of(z80_t *cpu, z80_reg which)
{
    switch (which)
    {
    case Z80_REG_AF:
        return &cpu->af;
    case Z80_REG_BC:
        return &cpu->bc;
    case Z80_REG_DE:
        return &cpu->de;
    case Z80_REG_HL:
        return &cpu->hl;
    case Z80_REG_AF_ALT:
        return &cpu->af_alt;
    case Z80_REG_BC_ALT:
        return &cpu->bc_alt;
    case Z80_REG_DE_ALT:
        return &cpu->de_alt;
    case Z80_REG_HL_ALT:
        return &cpu->hl_alt;
    case Z80_REG_IX:
        return &cpu->ix;
    case Z80_REG_IY:
        return &cpu->iy;
    case Z80_REG_SP:
        return &cpu->sp;
    case Z80_REG_PC:
        return &cpu->pc;
    case Z80_REG_WZ:
        return &cpu->wz;
    default:
        return NULL;
    }
}

uint16_t z80_get(const z80_t *cpu, z80_reg which)
{
    if (!cpu)
    {
        return 0;
    }
    if (Z80_REG_IR == which)
    {
        return (uint16_t)(((uint16_t)cpu->i << 8) | cpu->r);
    }
    const z80_pair *pair = pair_of((z80_t *)cpu, which);
    return pair ? pair->word : 0;
}

void z80_set(z80_t *cpu, z80_reg which, uint16_t value)
{
    if (!cpu)
    {
        return;
    }
    if (Z80_REG_IR == which)
    {
        cpu->i = (uint8_t)(value >> 8);
        cpu->r = (uint8_t)value;
        return;
    }
    z80_pair *pair = pair_of(cpu, which);
    if (pair)
    {
        pair->word = value;
    }
}

void z80_state(const z80_t *cpu, z80_state_t *out)
{
    if (!cpu || !out)
    {
        return;
    }
    memset(out, 0, sizeof *out);

    out->af = cpu->af.word;
    out->bc = cpu->bc.word;
    out->de = cpu->de.word;
    out->hl = cpu->hl.word;
    out->af_alt = cpu->af_alt.word;
    out->bc_alt = cpu->bc_alt.word;
    out->de_alt = cpu->de_alt.word;
    out->hl_alt = cpu->hl_alt.word;
    out->ix = cpu->ix.word;
    out->iy = cpu->iy.word;
    out->sp = cpu->sp.word;
    out->pc = cpu->pc.word;
    out->wz = cpu->wz.word;
    out->i = cpu->i;
    out->r = cpu->r;
    out->im = cpu->im;
    out->iff1 = cpu->iff1;
    out->iff2 = cpu->iff2;
    out->halted = cpu->halted;
    out->edges = cpu->edges;
}

const char *z80_reg_name(z80_reg which)
{
    static const char *const names[Z80_REG_COUNT] = {"AF",  "BC", "DE", "HL", "AF'", "BC'", "DE'",
                                                     "HL'", "IX", "IY", "SP", "PC",  "WZ",  "IR"};
    return (which < Z80_REG_COUNT) ? names[which] : "??";
}

/* ---------------------------------------------------------------- */
/* Snapshots                                                         */
/* ---------------------------------------------------------------- */

/**
 * What a snapshot holds. The engine's position is in here as well as the
 * registers: the sequence is stored as an index rather than a pointer, so a
 * snapshot does not depend on where the program was loaded.
 */
typedef struct
{
    uint32_t magic;
    uint32_t version;

    uint16_t af, bc, de, hl;
    uint16_t af_alt, bc_alt, de_alt, hl_alt;
    uint16_t ix, iy, sp, pc, wz;
    uint8_t i, r, im;
    uint8_t iff1, iff2, halted;

    uint8_t in_fetch; /**< 1 while the M1 sequence is running */
    uint8_t step;
    uint8_t opcode;
    uint8_t clk;
    uint64_t edges;
    uint64_t unimplemented;
} z80_snapshot;

size_t z80_snapshot_size(void)
{
    return sizeof(z80_snapshot);
}

size_t z80_save(const z80_t *cpu, void *buffer, size_t size)
{
    if (!cpu || !buffer || size < sizeof(z80_snapshot))
    {
        return 0;
    }

    z80_snapshot snapshot;
    memset(&snapshot, 0, sizeof snapshot);

    snapshot.magic = Z80_SNAPSHOT_MAGIC;
    snapshot.version = Z80_SNAPSHOT_VERSION;
    snapshot.af = cpu->af.word;
    snapshot.bc = cpu->bc.word;
    snapshot.de = cpu->de.word;
    snapshot.hl = cpu->hl.word;
    snapshot.af_alt = cpu->af_alt.word;
    snapshot.bc_alt = cpu->bc_alt.word;
    snapshot.de_alt = cpu->de_alt.word;
    snapshot.hl_alt = cpu->hl_alt.word;
    snapshot.ix = cpu->ix.word;
    snapshot.iy = cpu->iy.word;
    snapshot.sp = cpu->sp.word;
    snapshot.pc = cpu->pc.word;
    snapshot.wz = cpu->wz.word;
    snapshot.i = cpu->i;
    snapshot.r = cpu->r;
    snapshot.im = cpu->im;
    snapshot.iff1 = cpu->iff1 ? 1u : 0u;
    snapshot.iff2 = cpu->iff2 ? 1u : 0u;
    snapshot.halted = cpu->halted ? 1u : 0u;
    snapshot.in_fetch = (cpu->seq == &m1_fetch_seq) ? 1u : 0u;
    snapshot.step = cpu->step;
    snapshot.opcode = cpu->opcode;
    snapshot.clk = cpu->clk;
    snapshot.edges = cpu->edges;
    snapshot.unimplemented = cpu->unimplemented;

    memcpy(buffer, &snapshot, sizeof snapshot);
    return sizeof snapshot;
}

bool z80_load(z80_t *cpu, const void *buffer, size_t size)
{
    if (!cpu || !buffer || size < sizeof(z80_snapshot))
    {
        return false;
    }

    z80_snapshot snapshot;
    memcpy(&snapshot, buffer, sizeof snapshot);

    if (Z80_SNAPSHOT_MAGIC != snapshot.magic || Z80_SNAPSHOT_VERSION != snapshot.version)
    {
        return false;
    }

    cpu->af.word = snapshot.af;
    cpu->bc.word = snapshot.bc;
    cpu->de.word = snapshot.de;
    cpu->hl.word = snapshot.hl;
    cpu->af_alt.word = snapshot.af_alt;
    cpu->bc_alt.word = snapshot.bc_alt;
    cpu->de_alt.word = snapshot.de_alt;
    cpu->hl_alt.word = snapshot.hl_alt;
    cpu->ix.word = snapshot.ix;
    cpu->iy.word = snapshot.iy;
    cpu->sp.word = snapshot.sp;
    cpu->pc.word = snapshot.pc;
    cpu->wz.word = snapshot.wz;
    cpu->i = snapshot.i;
    cpu->r = snapshot.r;
    cpu->im = snapshot.im;
    cpu->iff1 = 0 != snapshot.iff1;
    cpu->iff2 = 0 != snapshot.iff2;
    cpu->halted = 0 != snapshot.halted;
    cpu->opcode = snapshot.opcode;
    cpu->clk = snapshot.clk;
    cpu->edges = snapshot.edges;
    cpu->unimplemented = snapshot.unimplemented;

    /* Restore where in the machine cycle it was. Anything that is not the
       fetch is a decoded instruction, which is found from the opcode. */
    if (snapshot.in_fetch)
    {
        cpu->seq = &m1_fetch_seq;
    }
    else
    {
        const z80_seq *decoded = decode(cpu);
        cpu->seq = decoded ? decoded : &m1_fetch_seq;
    }
    cpu->step = (snapshot.step < cpu->seq->count) ? snapshot.step : 0u;

    return true;
}
