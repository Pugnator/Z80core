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
 * PHASE 2: the fetch, the memory read and write cycles and WAIT are real, and
 * an instruction is a list of machine cycles with an effect at the end. The
 * instruction set is filling in from there; what is missing costs its fetch
 * and increments a counter (z80_unimplemented) so the gap is visible.
 */

#include "z80core.h"

#include <stdlib.h>
#include <string.h>

#define Z80CORE_VERSION "0.3.0-phase2"

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
    const struct z80_seq *seq; /**< machine cycle being executed */
    uint8_t step;              /**< cursor into it */
    uint8_t opcode;            /**< what the last fetch latched */
    uint8_t clk;               /**< the level the core last advanced to */

    /* the instruction in progress, and the bus cycle it is running */
    const struct z80_instr *instr;
    uint8_t cycle;     /**< which of the instruction's cycles */
    uint16_t bus_addr; /**< address the current read or write uses */
    uint8_t bus_data;  /**< byte read, or byte to write */

    uint64_t edges;
    uint64_t unimplemented;
};

/**
 * A step runs on one clock edge and may drive pins. Steps never touch the
 * cursor; the engine owns it.
 */
typedef void (*z80_step_fn)(z80_t *cpu, z80_pins_t *pins);

/** A machine cycle, as a list of per-edge steps. */
typedef struct z80_seq
{
    /**
     * Run when the cycle begins and ends. Neither consumes a clock edge: they
     * are where an instruction says which address a generic read or write
     * uses, and what to do with the byte afterwards. Doing that as a step
     * instead would make every such cycle half a T-state too long.
     */
    z80_step_fn enter;
    z80_step_fn exit;
    const z80_step_fn *steps;
    uint8_t count;
    /**
     * Index of the step that samples WAIT, or 0xFF for a cycle that cannot be
     * stretched. On that edge, a host holding WAIT makes the core repeat the
     * T-state instead of advancing - which is the whole of wait-state support.
     */
    uint8_t wait_step;
} z80_seq;

#define Z80_NO_WAIT 0xFFu

/** What an instruction is: bus cycles after the fetch, then an effect. */
typedef struct z80_instr
{
    const z80_seq *cycles[3];
    uint8_t cycle_count;
    /** Runs when the last cycle finishes; where the registers change. */
    void (*execute)(z80_t *cpu, z80_pins_t *pins);
} z80_instr;

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

/* WAIT is sampled on the falling edge of T2, which is step 3 */
static const z80_seq m1_fetch_seq = {NULL, NULL, m1_fetch_steps,
                                     (uint8_t)(sizeof m1_fetch_steps / sizeof m1_fetch_steps[0]), 3u};

/* ---------------------------------------------------------------- */
/* Memory cycles                                                     */
/* ---------------------------------------------------------------- */

static void step_addr_out(z80_t *cpu, z80_pins_t *pins)
{
    pins->A = cpu->bus_addr;
}

static void step_read_assert(z80_t *cpu, z80_pins_t *pins)
{
    (void)cpu;
    pins->ctrl |= Z80_MREQ | Z80_RD;
}

static void step_read_sample(z80_t *cpu, z80_pins_t *pins)
{
    cpu->bus_data = pins->D;
}

static void step_read_release(z80_t *cpu, z80_pins_t *pins)
{
    (void)cpu;
    pins->ctrl &= ~(uint32_t)(Z80_MREQ | Z80_RD);
}

static void step_write_assert_mreq(z80_t *cpu, z80_pins_t *pins)
{
    (void)cpu;
    pins->ctrl |= Z80_MREQ;
}

static void step_write_data_out(z80_t *cpu, z80_pins_t *pins)
{
    pins->D = cpu->bus_data;
}

/**
 * WR falls half a cycle after the data appears, which is the ordering the
 * A-Z80 table shows: T2 carries MREQ with the data on the bus, T3 adds WR.
 */
static void step_write_assert_wr(z80_t *cpu, z80_pins_t *pins)
{
    (void)cpu;
    pins->ctrl |= Z80_WR;
}

static void step_write_release(z80_t *cpu, z80_pins_t *pins)
{
    (void)cpu;
    pins->ctrl &= ~(uint32_t)(Z80_MREQ | Z80_WR);
}

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
static const z80_seq halt_seq = {NULL, NULL, halt_steps, 1u, Z80_NO_WAIT};

/**
 * The eight places the register field of an opcode can name. Index 6 is (HL),
 * which is memory rather than a register: an instruction naming it does a bus
 * cycle, which is why those forms cost more T-states.
 */
static uint8_t *register_slot(z80_t *cpu, uint8_t index)
{
    switch (index & 7u)
    {
    case 0:
        return &cpu->bc.byte.high; /* B */
    case 1:
        return &cpu->bc.byte.low; /* C */
    case 2:
        return &cpu->de.byte.high; /* D */
    case 3:
        return &cpu->de.byte.low; /* E */
    case 4:
        return &cpu->hl.byte.high; /* H */
    case 5:
        return &cpu->hl.byte.low; /* L */
    case 7:
        return &cpu->af.byte.high; /* A */
    default:
        return NULL; /* 6 is (HL) */
    }
}

/** LD r,r' - both halves of the opcode name a register. */
static void execute_ld_reg_reg(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    const uint8_t *source = register_slot(cpu, cpu->opcode);
    uint8_t *destination = register_slot(cpu, (uint8_t)(cpu->opcode >> 3));
    if (source && destination)
    {
        *destination = *source;
    }
}

/** LD r,n - the byte the read cycle collected. */
static void execute_ld_reg_immediate(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    uint8_t *destination = register_slot(cpu, (uint8_t)(cpu->opcode >> 3));
    if (destination)
    {
        *destination = cpu->bus_data;
    }
}

/** LD r,(HL) - the read used HL as its address. */
static void execute_ld_reg_from_hl(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    uint8_t *destination = register_slot(cpu, (uint8_t)(cpu->opcode >> 3));
    if (destination)
    {
        *destination = cpu->bus_data;
    }
}

/** JP nn - two reads brought the address in low byte first. */
static void execute_jump(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->pc.word = cpu->wz.word;
}

/** An opcode the core does not implement yet: costs its fetch, does nothing. */
static void execute_unimplemented(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    ++cpu->unimplemented;
}

/*
 * Preparing a bus cycle means saying where it goes. These run at the end of
 * the cycle before, which is when the address is known.
 */

static void step_prepare_pc_read(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->bus_addr = cpu->pc.word;
    cpu->pc.word = (uint16_t)(cpu->pc.word + 1u);
}

static void step_prepare_hl_access(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->bus_addr = cpu->hl.word;
}

static void step_store_low_byte(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->wz.byte.low = cpu->bus_data;
}

static void step_store_high_byte(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->wz.byte.high = cpu->bus_data;
}

static void step_write_from_register(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    const uint8_t *source = register_slot(cpu, cpu->opcode);
    cpu->bus_addr = cpu->hl.word;
    cpu->bus_data = source ? *source : 0u;
}

/* The canonical cycles of spec 5.3, shared by every instruction that uses
   them. What differs between instructions is the entry and exit hooks. */

static const z80_step_fn mem_read_steps[] = {
    step_addr_out,    /* T1 rise: address out       */
    step_read_assert, /* T1 fall: MREQ, RD asserted */
    step_nothing,     /* T2 rise                    */
    step_nothing,     /* T2 fall: WAIT sampled here */
    step_read_sample, /* T3 rise: the byte is taken */
    step_read_release /* T3 fall: MREQ, RD released */
};

static const z80_step_fn mem_write_steps[] = {
    step_addr_out,          /* T1 rise: address out                    */
    step_write_assert_mreq, /* T1 fall: MREQ asserted                  */
    step_write_data_out,    /* T2 rise: data on the bus                */
    step_write_assert_wr,   /* T2 fall: WR asserted, WAIT sampled here */
    step_nothing,           /* T3 rise                                 */
    step_write_release      /* T3 fall: WR, MREQ released              */
};

static const z80_seq read_immediate_seq = {step_prepare_pc_read, NULL, mem_read_steps, 6u, 3u};
static const z80_seq read_hl_seq = {step_prepare_hl_access, NULL, mem_read_steps, 6u, 3u};
static const z80_seq read_low_seq = {step_prepare_pc_read, step_store_low_byte, mem_read_steps, 6u, 3u};
static const z80_seq read_high_seq = {step_prepare_pc_read, step_store_high_byte, mem_read_steps, 6u, 3u};
static const z80_seq write_hl_seq = {step_write_from_register, NULL, mem_write_steps, 6u, 3u};

static const z80_instr instr_nop = {{NULL}, 0u, NULL};
static const z80_instr instr_halt = {{&halt_seq}, 1u, NULL};
static const z80_instr instr_ld_reg_reg = {{NULL}, 0u, execute_ld_reg_reg};
static const z80_instr instr_ld_reg_immediate = {{&read_immediate_seq}, 1u, execute_ld_reg_immediate};
static const z80_instr instr_ld_reg_from_hl = {{&read_hl_seq}, 1u, execute_ld_reg_from_hl};
static const z80_instr instr_ld_hl_from_reg = {{&write_hl_seq}, 1u, NULL};
static const z80_instr instr_jump = {{&read_low_seq, &read_high_seq}, 2u, execute_jump};
static const z80_instr instr_unimplemented = {{NULL}, 0u, execute_unimplemented};

/**
 * @brief Pick what runs after a fetch.
 *
 * The opcode's bit fields do the work, as they do on the real part: bits 6-7
 * select the group, 3-5 the destination, 0-2 the source.
 */
static const z80_instr *decode(const z80_t *cpu)
{
    const uint8_t opcode = cpu->opcode;

    if (0x00u == opcode)
    {
        return &instr_nop;
    }
    if (0x76u == opcode)
    {
        return &instr_halt; /* the hole in the LD r,r' block */
    }
    if (0xC3u == opcode)
    {
        return &instr_jump;
    }

    /* 01 ddd sss : LD r,r' and the (HL) forms */
    if (0x40u == (opcode & 0xC0u))
    {
        const uint8_t destination = (uint8_t)((opcode >> 3) & 7u);
        const uint8_t source = (uint8_t)(opcode & 7u);
        if (6u == source && 6u != destination)
        {
            return &instr_ld_reg_from_hl;
        }
        if (6u == destination)
        {
            return &instr_ld_hl_from_reg;
        }
        return &instr_ld_reg_reg;
    }

    /* 00 ddd 110 : LD r,n */
    if (0x06u == (opcode & 0xC7u) && 6u != ((opcode >> 3) & 7u))
    {
        return &instr_ld_reg_immediate;
    }

    return &instr_unimplemented;
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

    const uint8_t executed = cpu->step;
    cpu->seq->steps[executed](cpu, pins);
    ++cpu->edges;

    /* A host holding WAIT at the sampling edge stretches the cycle: the core
       repeats the T-state, holding every pin as it stands, and samples again
       on the next falling edge. Stepping back one edge is exactly that, since
       a T-state is two edges. */
    if (executed == cpu->seq->wait_step && (pins->ctrl & Z80_WAIT))
    {
        --cpu->step;
        return (pins->ctrl ^ ctrl_before) & Z80_OUTPUT_PINS;
    }

    ++cpu->step;

    if (cpu->step >= cpu->seq->count)
    {
        if (cpu->seq->exit)
        {
            cpu->seq->exit(cpu, pins);
        }

        if (cpu->seq == &m1_fetch_seq)
        {
            /* the fetch just ended: start the instruction it decoded */
            cpu->instr = decode(cpu);
            cpu->cycle = 0;
        }
        else
        {
            ++cpu->cycle;
        }

        if (cpu->instr && cpu->cycle < cpu->instr->cycle_count)
        {
            cpu->seq = cpu->instr->cycles[cpu->cycle];
        }
        else
        {
            /* every cycle is done, so the instruction takes effect and the
               next fetch begins */
            if (cpu->instr && cpu->instr->execute)
            {
                cpu->instr->execute(cpu, pins);
            }
            cpu->instr = NULL;
            cpu->seq = &m1_fetch_seq;
        }
        cpu->step = 0;

        if (cpu->seq->enter)
        {
            cpu->seq->enter(cpu, pins);
        }
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
    uint8_t cycle; /**< which of the instruction's cycles, when not fetching */
    uint8_t opcode;
    uint8_t clk;
    uint16_t bus_addr;
    uint8_t bus_data;
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
    snapshot.cycle = cpu->cycle;
    snapshot.opcode = cpu->opcode;
    snapshot.clk = cpu->clk;
    snapshot.bus_addr = cpu->bus_addr;
    snapshot.bus_data = cpu->bus_data;
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

    cpu->bus_addr = snapshot.bus_addr;
    cpu->bus_data = snapshot.bus_data;

    /* Restore where in the instruction it was. The sequence is not stored as a
       pointer - that would not survive a different build - but rebuilt from
       the opcode and the cycle index, which describe the same position. */
    if (snapshot.in_fetch)
    {
        cpu->instr = NULL;
        cpu->cycle = 0;
        cpu->seq = &m1_fetch_seq;
    }
    else
    {
        cpu->instr = decode(cpu);
        cpu->cycle = snapshot.cycle;
        if (cpu->instr && cpu->cycle < cpu->instr->cycle_count)
        {
            cpu->seq = cpu->instr->cycles[cpu->cycle];
        }
        else
        {
            cpu->instr = NULL;
            cpu->seq = &m1_fetch_seq;
        }
    }
    cpu->step = (snapshot.step < cpu->seq->count) ? snapshot.step : 0u;

    return true;
}
