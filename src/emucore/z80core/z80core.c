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
 * and selects what runs next. Adding an instruction means describing its
 * machine cycles and what it does around them - the engine itself never grows.
 *
 * PHASE 3: the whole unprefixed instruction set, the ALU and its flags, and
 * I/O cycles. The CB, DD, ED and FD prefixes still count as unimplemented, so
 * the gap is visible through z80_unimplemented() rather than silent.
 *
 * The separation that makes this work: a z80_seq is *pin choreography* and
 * nothing else - what a memory read looks like on the bus, edge by edge. A
 * z80_cycle pairs one of those with the hooks that give it meaning for one
 * instruction: where the address comes from, what to do with the byte. Timing
 * is therefore written once and cannot drift between instructions.
 */

#include "z80core.h"

#include <stdlib.h>
#include <string.h>

#define Z80CORE_VERSION "0.4.0-phase3"

/** Snapshot format, so a stale file is refused rather than misread. */
#define Z80_SNAPSHOT_MAGIC 0x5A383043u /* "Z80C" */
#define Z80_SNAPSHOT_VERSION 3u

/** Outputs the core drives; the rest of ctrl belongs to the host. */
#define Z80_OUTPUT_PINS (Z80_M1 | Z80_MREQ | Z80_IORQ | Z80_RD | Z80_WR | Z80_RFSH | Z80_HALT | Z80_BUSAK)

/** The bits of F. X and Y are undocumented copies of result bits 3 and 5. */
enum z80_flag
{
    Z80_FLAG_C = 0x01u,
    Z80_FLAG_N = 0x02u,
    Z80_FLAG_PV = 0x04u,
    Z80_FLAG_X = 0x08u,
    Z80_FLAG_H = 0x10u,
    Z80_FLAG_Y = 0x20u,
    Z80_FLAG_Z = 0x40u,
    Z80_FLAG_S = 0x80u
};

#define Z80_FLAG_XY (Z80_FLAG_X | Z80_FLAG_Y)

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
    uint8_t prefix;            /**< the prefix byte in force, or 0 */
    uint8_t clk;               /**< the level the core last advanced to */

    /* the instruction in progress, and the bus cycle it is running */
    const struct z80_instr *instr;
    uint8_t cycle;       /**< which of the instruction's cycles */
    uint8_t cycle_limit; /**< how many it will actually run; a condition may lower it */
    uint16_t bus_addr;   /**< address the current read or write uses */
    uint8_t bus_data;    /**< byte read, or byte to write */
    z80_pair tmp;        /**< 16-bit scratch, for operands WZ must not see */

    uint64_t edges;
    uint64_t unimplemented;
};

/**
 * A step runs on one clock edge and may drive pins. Steps never touch the
 * cursor; the engine owns it.
 */
typedef void (*z80_step_fn)(z80_t *cpu, z80_pins_t *pins);

/**
 * A machine cycle's pin choreography, and nothing else. Shared by every
 * instruction that performs this kind of bus cycle.
 */
typedef struct z80_seq
{
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

/**
 * One machine cycle of one instruction: the choreography, plus what this
 * instruction wants done before it starts and after it ends. Neither hook
 * consumes a clock edge - @c enter is where an instruction says which address
 * a generic read or write uses, @c exit what to do with the byte afterwards.
 * Doing either as a step would make the cycle half a T-state too long.
 */
typedef struct
{
    const z80_seq *seq;
    z80_step_fn enter;
    z80_step_fn exit;
} z80_cycle;

/** The most machine cycles any unprefixed instruction needs (EX (SP),HL). */
#define Z80_MAX_CYCLES 6

/** What an instruction is: bus cycles after the fetch, then an effect. */
typedef struct z80_instr
{
    z80_cycle cycles[Z80_MAX_CYCLES];
    uint8_t cycle_count;
    /**
     * Runs when the last cycle finishes. It runs whether or not a condition
     * cut the instruction short, so anything conditional belongs in a hook of
     * a cycle that only happens when the condition holds - not here.
     */
    void (*execute)(z80_t *cpu, z80_pins_t *pins);
} z80_instr;

static const z80_seq m1_fetch_seq;

/**
 * Called from an exit hook: this instruction ends after the cycle now
 * finishing. It is how a taken/not-taken branch costs different T-states.
 */
static void end_instruction_here(z80_t *cpu)
{
    cpu->cycle_limit = (uint8_t)(cpu->cycle + 1u);
}

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
 *
 * Instructions the manual describes as having a five- or six-T-state M1 are
 * built here as this four-T-state fetch followed by an idle cycle. The pins
 * are identical either way - the refresh has already been released - and it
 * keeps one fetch sequence rather than several.
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
static const z80_seq m1_fetch_seq = {m1_fetch_steps, (uint8_t)(sizeof m1_fetch_steps / sizeof m1_fetch_steps[0]), 3u};

/* ---------------------------------------------------------------- */
/* Bus cycles: memory, I/O, and doing nothing at all                 */
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

static void step_io_assert_rd(z80_t *cpu, z80_pins_t *pins)
{
    (void)cpu;
    pins->ctrl |= Z80_IORQ | Z80_RD;
}

static void step_io_assert_wr(z80_t *cpu, z80_pins_t *pins)
{
    (void)cpu;
    pins->ctrl |= Z80_IORQ | Z80_WR;
}

static void step_io_release(z80_t *cpu, z80_pins_t *pins)
{
    (void)cpu;
    pins->ctrl &= ~(uint32_t)(Z80_IORQ | Z80_RD | Z80_WR);
}

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

/**
 * An I/O cycle is four T-states, not three. IORQ and RD or WR go active a
 * whole clock after the address appears rather than half of one, which is the
 * time the part gives an I/O device to decode the address; the extra T-state
 * (TW) is a wait state the CPU inserts by itself. A host holding WAIT adds
 * more, sampled on TW's falling edge.
 */
static const z80_step_fn io_read_steps[] = {
    step_addr_out,     /* T1 rise: address out            */
    step_nothing,      /* T1 fall                         */
    step_io_assert_rd, /* T2 rise: IORQ and RD asserted   */
    step_nothing,      /* T2 fall                         */
    step_nothing,      /* TW rise: the automatic wait     */
    step_nothing,      /* TW fall: WAIT sampled here      */
    step_read_sample,  /* T3 rise: the byte is taken      */
    step_io_release    /* T3 fall: IORQ and RD released   */
};

static const z80_step_fn io_write_steps[] = {
    step_addr_out,       /* T1 rise: address out          */
    step_write_data_out, /* T1 fall: data on the bus      */
    step_io_assert_wr,   /* T2 rise: IORQ and WR asserted */
    step_nothing,        /* T2 fall                       */
    step_nothing,        /* TW rise: the automatic wait   */
    step_nothing,        /* TW fall: WAIT sampled here    */
    step_nothing,        /* T3 rise                       */
    step_io_release      /* T3 fall: IORQ and WR released */
};

/**
 * T-states with no bus activity, for the internal work the manual counts but
 * never shows on a pin. One array serves every length; the count picks it.
 */
static const z80_step_fn idle_steps[] = {step_nothing, step_nothing, step_nothing, step_nothing, step_nothing,
                                         step_nothing, step_nothing, step_nothing, step_nothing, step_nothing,
                                         step_nothing, step_nothing, step_nothing, step_nothing};

static const z80_seq mem_read_seq = {mem_read_steps, 6u, 3u};
static const z80_seq mem_write_seq = {mem_write_steps, 6u, 3u};
static const z80_seq io_read_seq = {io_read_steps, 8u, 5u};
static const z80_seq io_write_seq = {io_write_steps, 8u, 5u};
static const z80_seq idle1_seq = {idle_steps, 2u, Z80_NO_WAIT};
static const z80_seq idle2_seq = {idle_steps, 4u, Z80_NO_WAIT};
static const z80_seq idle5_seq = {idle_steps, 10u, Z80_NO_WAIT};
static const z80_seq idle7_seq = {idle_steps, 14u, Z80_NO_WAIT};

/* ---------------------------------------------------------------- */
/* The ALU and its flags                                             */
/* ---------------------------------------------------------------- */

static bool parity_even(uint8_t value)
{
    value ^= (uint8_t)(value >> 4);
    value ^= (uint8_t)(value >> 2);
    value ^= (uint8_t)(value >> 1);
    return 0u == (value & 1u);
}

/** S, Z and the two undocumented copies, which almost every result sets. */
static uint8_t flags_sz_xy(uint8_t result)
{
    uint8_t flags = (uint8_t)(result & (Z80_FLAG_S | Z80_FLAG_XY));
    if (0u == result)
    {
        flags |= Z80_FLAG_Z;
    }
    return flags;
}

static void alu_add(z80_t *cpu, uint8_t value, uint8_t carry)
{
    const uint8_t a = cpu->af.byte.high;
    const uint16_t wide = (uint16_t)(a + value + carry);
    const uint8_t result = (uint8_t)wide;

    uint8_t flags = flags_sz_xy(result);
    if ((a ^ value ^ result) & 0x10u)
    {
        flags |= Z80_FLAG_H;
    }
    if ((a ^ result) & (value ^ result) & 0x80u)
    {
        flags |= Z80_FLAG_PV;
    }
    if (wide & 0x100u)
    {
        flags |= Z80_FLAG_C;
    }

    cpu->af.byte.high = result;
    cpu->af.byte.low = flags;
}

/** Shared by SUB, SBC and CP, which differ only in what they keep. */
static uint8_t sub_flags(uint8_t a, uint8_t value, uint8_t carry, uint8_t *result_out)
{
    const uint16_t wide = (uint16_t)(a - value - carry);
    const uint8_t result = (uint8_t)wide;

    uint8_t flags = (uint8_t)(flags_sz_xy(result) | Z80_FLAG_N);
    if ((a ^ value ^ result) & 0x10u)
    {
        flags |= Z80_FLAG_H;
    }
    if ((a ^ value) & (a ^ result) & 0x80u)
    {
        flags |= Z80_FLAG_PV;
    }
    if (wide & 0x100u)
    {
        flags |= Z80_FLAG_C;
    }

    *result_out = result;
    return flags;
}

static void alu_sub(z80_t *cpu, uint8_t value, uint8_t carry)
{
    uint8_t result = 0u;
    const uint8_t flags = sub_flags(cpu->af.byte.high, value, carry, &result);
    cpu->af.byte.high = result;
    cpu->af.byte.low = flags;
}

/**
 * CP is SUB that throws the result away - so X and Y, which everywhere else
 * copy the result, here copy the operand instead. Nothing else does this.
 */
static void alu_cp(z80_t *cpu, uint8_t value)
{
    uint8_t result = 0u;
    uint8_t flags = sub_flags(cpu->af.byte.high, value, 0u, &result);
    flags = (uint8_t)((flags & ~(uint8_t)Z80_FLAG_XY) | (value & Z80_FLAG_XY));
    cpu->af.byte.low = flags;
}

static void alu_logic(z80_t *cpu, uint8_t result, uint8_t half_carry)
{
    uint8_t flags = (uint8_t)(flags_sz_xy(result) | half_carry);
    if (parity_even(result))
    {
        flags |= Z80_FLAG_PV;
    }
    cpu->af.byte.high = result;
    cpu->af.byte.low = flags;
}

/** INC and DEC leave carry alone, which is why they are not ADD and SUB. */
static uint8_t alu_inc(z80_t *cpu, uint8_t value)
{
    const uint8_t result = (uint8_t)(value + 1u);
    uint8_t flags = (uint8_t)((cpu->af.byte.low & Z80_FLAG_C) | flags_sz_xy(result));
    if (0x00u == (result & 0x0Fu))
    {
        flags |= Z80_FLAG_H;
    }
    if (0x80u == result)
    {
        flags |= Z80_FLAG_PV;
    }
    cpu->af.byte.low = flags;
    return result;
}

static uint8_t alu_dec(z80_t *cpu, uint8_t value)
{
    const uint8_t result = (uint8_t)(value - 1u);
    uint8_t flags = (uint8_t)((cpu->af.byte.low & Z80_FLAG_C) | Z80_FLAG_N | flags_sz_xy(result));
    if (0x0Fu == (result & 0x0Fu))
    {
        flags |= Z80_FLAG_H;
    }
    if (0x7Fu == result)
    {
        flags |= Z80_FLAG_PV;
    }
    cpu->af.byte.low = flags;
    return result;
}

/**
 * ADD HL,rr carries from bit 11 into H and bit 15 into C, and leaves S, Z and
 * P/V untouched: it is a 16-bit add built from two 8-bit ones, and the flags
 * show it.
 */
static void alu_add16(z80_t *cpu, z80_pair *destination, uint16_t value)
{
    const uint16_t a = destination->word;
    const uint32_t wide = (uint32_t)a + value;
    const uint16_t result = (uint16_t)wide;

    uint8_t flags = (uint8_t)(cpu->af.byte.low & (Z80_FLAG_S | Z80_FLAG_Z | Z80_FLAG_PV));
    flags |= (uint8_t)((result >> 8) & Z80_FLAG_XY);
    if ((a ^ value ^ result) & 0x1000u)
    {
        flags |= Z80_FLAG_H;
    }
    if (wide & 0x10000u)
    {
        flags |= Z80_FLAG_C;
    }

    cpu->wz.word = (uint16_t)(a + 1u);
    destination->word = result;
    cpu->af.byte.low = flags;
}

/**
 * The four accumulator rotates. Unlike their CB-prefixed cousins they leave
 * S, Z and P/V alone and never test the result.
 */
static void alu_rotate_a(z80_t *cpu, uint8_t which)
{
    const uint8_t a = cpu->af.byte.high;
    const uint8_t carry_in = (uint8_t)(cpu->af.byte.low & Z80_FLAG_C);
    uint8_t result = 0u;
    bool carry_out = false;

    switch (which & 3u)
    {
    case 0: /* RLCA */
        result = (uint8_t)((a << 1) | (a >> 7));
        carry_out = 0u != (a & 0x80u);
        break;
    case 1: /* RRCA */
        result = (uint8_t)((a >> 1) | (a << 7));
        carry_out = 0u != (a & 0x01u);
        break;
    case 2: /* RLA */
        result = (uint8_t)((a << 1) | (carry_in ? 1u : 0u));
        carry_out = 0u != (a & 0x80u);
        break;
    default: /* RRA */
        result = (uint8_t)((a >> 1) | (carry_in ? 0x80u : 0u));
        carry_out = 0u != (a & 0x01u);
        break;
    }

    uint8_t flags = (uint8_t)(cpu->af.byte.low & (Z80_FLAG_S | Z80_FLAG_Z | Z80_FLAG_PV));
    flags |= (uint8_t)(result & Z80_FLAG_XY);
    if (carry_out)
    {
        flags |= Z80_FLAG_C;
    }

    cpu->af.byte.high = result;
    cpu->af.byte.low = flags;
}

/**
 * DAA corrects A after a decimal add or subtract. It reads N and H to learn
 * what the previous operation was, which is the only reason those flags exist.
 */
static void alu_daa(z80_t *cpu)
{
    const uint8_t a = cpu->af.byte.high;
    const uint8_t before = cpu->af.byte.low;
    const bool subtracting = 0u != (before & Z80_FLAG_N);
    uint8_t adjust = 0u;
    uint8_t carry = (uint8_t)(before & Z80_FLAG_C);

    if ((before & Z80_FLAG_H) || (a & 0x0Fu) > 9u)
    {
        adjust |= 0x06u;
    }
    if (carry || a > 0x99u)
    {
        adjust |= 0x60u;
        carry = Z80_FLAG_C;
    }

    const uint8_t result = subtracting ? (uint8_t)(a - adjust) : (uint8_t)(a + adjust);

    uint8_t flags = (uint8_t)(flags_sz_xy(result) | (before & Z80_FLAG_N) | carry);
    if (parity_even(result))
    {
        flags |= Z80_FLAG_PV;
    }
    if (subtracting ? ((before & Z80_FLAG_H) && (a & 0x0Fu) < 6u) : ((a & 0x0Fu) > 9u))
    {
        flags |= Z80_FLAG_H;
    }

    cpu->af.byte.high = result;
    cpu->af.byte.low = flags;
}

/**
 * The eight CB shifts and rotates. Unlike the accumulator rotates these test
 * the result, so S, Z and parity all move.
 *
 * Index 6 is SLL, which Zilog never documented: it shifts left and puts a 1 in
 * at the bottom. It sits in the middle of an otherwise regular block, so
 * leaving it out would cost more code than putting it in.
 */
static uint8_t alu_shift(z80_t *cpu, uint8_t which, uint8_t value)
{
    const uint8_t carry_in = (uint8_t)((cpu->af.byte.low & Z80_FLAG_C) ? 1u : 0u);
    uint8_t result = 0u;
    bool carry_out = false;

    switch (which & 7u)
    {
    case 0: /* RLC */
        result = (uint8_t)((value << 1) | (value >> 7));
        carry_out = 0u != (value & 0x80u);
        break;
    case 1: /* RRC */
        result = (uint8_t)((value >> 1) | (value << 7));
        carry_out = 0u != (value & 0x01u);
        break;
    case 2: /* RL */
        result = (uint8_t)((value << 1) | carry_in);
        carry_out = 0u != (value & 0x80u);
        break;
    case 3: /* RR */
        result = (uint8_t)((value >> 1) | (carry_in ? 0x80u : 0u));
        carry_out = 0u != (value & 0x01u);
        break;
    case 4: /* SLA */
        result = (uint8_t)(value << 1);
        carry_out = 0u != (value & 0x80u);
        break;
    case 5: /* SRA - arithmetic, so the sign bit is kept */
        result = (uint8_t)((value >> 1) | (value & 0x80u));
        carry_out = 0u != (value & 0x01u);
        break;
    case 6: /* SLL */
        result = (uint8_t)((value << 1) | 1u);
        carry_out = 0u != (value & 0x80u);
        break;
    default: /* SRL */
        result = (uint8_t)(value >> 1);
        carry_out = 0u != (value & 0x01u);
        break;
    }

    uint8_t flags = flags_sz_xy(result);
    if (parity_even(result))
    {
        flags |= Z80_FLAG_PV;
    }
    if (carry_out)
    {
        flags |= Z80_FLAG_C;
    }

    cpu->af.byte.low = flags;
    return result;
}

/**
 * BIT tests one bit and reports it in Z, and in P/V as well - the only place
 * those two always agree. S follows only when bit 7 is the one tested.
 *
 * X and Y come from @p undocumented, which is the operand for the register
 * forms and the high half of WZ when the operand was memory. The result is not
 * kept, so there is nothing else for them to copy.
 */
static void alu_bit(z80_t *cpu, uint8_t bit, uint8_t value, uint8_t undocumented)
{
    const uint8_t masked = (uint8_t)(value & (uint8_t)(1u << (bit & 7u)));

    uint8_t flags = (uint8_t)((cpu->af.byte.low & Z80_FLAG_C) | Z80_FLAG_H);
    flags |= (uint8_t)(masked & Z80_FLAG_S);
    if (0u == masked)
    {
        flags |= (uint8_t)(Z80_FLAG_Z | Z80_FLAG_PV);
    }
    flags |= (uint8_t)(undocumented & Z80_FLAG_XY);

    cpu->af.byte.low = flags;
}

static bool condition_met(const z80_t *cpu, uint8_t code)
{
    const uint8_t flags = cpu->af.byte.low;
    switch (code & 7u)
    {
    case 0:
        return 0u == (flags & Z80_FLAG_Z); /* NZ */
    case 1:
        return 0u != (flags & Z80_FLAG_Z); /* Z  */
    case 2:
        return 0u == (flags & Z80_FLAG_C); /* NC */
    case 3:
        return 0u != (flags & Z80_FLAG_C); /* C  */
    case 4:
        return 0u == (flags & Z80_FLAG_PV); /* PO */
    case 5:
        return 0u != (flags & Z80_FLAG_PV); /* PE */
    case 6:
        return 0u == (flags & Z80_FLAG_S); /* P  */
    default:
        return 0u != (flags & Z80_FLAG_S); /* M  */
    }
}

/* ---------------------------------------------------------------- */
/* Naming the operands an opcode's bit fields select                 */
/* ---------------------------------------------------------------- */

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

/** Bits 5-4 name a pair: BC, DE, HL, SP - the set the arithmetic uses. */
static z80_pair *pair_rp(z80_t *cpu, uint8_t opcode)
{
    switch ((opcode >> 4) & 3u)
    {
    case 0:
        return &cpu->bc;
    case 1:
        return &cpu->de;
    case 2:
        return &cpu->hl;
    default:
        return &cpu->sp;
    }
}

/** The same field for PUSH and POP, where the fourth pair is AF, not SP. */
static z80_pair *pair_rp2(z80_t *cpu, uint8_t opcode)
{
    switch ((opcode >> 4) & 3u)
    {
    case 0:
        return &cpu->bc;
    case 1:
        return &cpu->de;
    case 2:
        return &cpu->hl;
    default:
        return &cpu->af;
    }
}

/** Apply one of the eight ALU operations bits 5-3 select. */
static void alu_operation(z80_t *cpu, uint8_t which, uint8_t value)
{
    const uint8_t carry = (uint8_t)((cpu->af.byte.low & Z80_FLAG_C) ? 1u : 0u);

    switch (which & 7u)
    {
    case 0:
        alu_add(cpu, value, 0u);
        break;
    case 1:
        alu_add(cpu, value, carry);
        break;
    case 2:
        alu_sub(cpu, value, 0u);
        break;
    case 3:
        alu_sub(cpu, value, carry);
        break;
    case 4:
        alu_logic(cpu, (uint8_t)(cpu->af.byte.high & value), Z80_FLAG_H);
        break;
    case 5:
        alu_logic(cpu, (uint8_t)(cpu->af.byte.high ^ value), 0u);
        break;
    case 6:
        alu_logic(cpu, (uint8_t)(cpu->af.byte.high | value), 0u);
        break;
    default:
        alu_cp(cpu, value);
        break;
    }
}

/* ---------------------------------------------------------------- */
/* Hooks: where a cycle's address comes from, and what it means      */
/* ---------------------------------------------------------------- */

static void at_pc(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->bus_addr = cpu->pc.word;
    cpu->pc.word = (uint16_t)(cpu->pc.word + 1u);
}

static void at_hl(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->bus_addr = cpu->hl.word;
}

static void at_bc(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->bus_addr = cpu->bc.word;
}

static void at_de(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->bus_addr = cpu->de.word;
}

static void at_tmp(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->bus_addr = cpu->tmp.word;
}

static void at_tmp_high(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->bus_addr = (uint16_t)(cpu->tmp.word + 1u);
}

static void at_sp(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->bus_addr = cpu->sp.word;
}

static void at_sp_high(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->bus_addr = (uint16_t)(cpu->sp.word + 1u);
}

/** A pop reads from the stack and then uncovers the next slot. */
static void at_pop(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->bus_addr = cpu->sp.word;
    cpu->sp.word = (uint16_t)(cpu->sp.word + 1u);
}

/** A push makes room first, so the byte lands below what was there. */
static void push_at(z80_t *cpu, uint8_t value)
{
    cpu->sp.word = (uint16_t)(cpu->sp.word - 1u);
    cpu->bus_addr = cpu->sp.word;
    cpu->bus_data = value;
}

static void enter_push_pc_high(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    push_at(cpu, cpu->pc.byte.high);
}

static void enter_push_pc_low(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    push_at(cpu, cpu->pc.byte.low);
}

static void enter_push_pair_high(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    push_at(cpu, pair_rp2(cpu, cpu->opcode)->byte.high);
}

static void enter_push_pair_low(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    push_at(cpu, pair_rp2(cpu, cpu->opcode)->byte.low);
}

/* collecting a byte that has just arrived */

static void exit_to_tmp_low(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->tmp.byte.low = cpu->bus_data;
}

static void exit_to_tmp_high(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->tmp.byte.high = cpu->bus_data;
}

static void exit_to_wz_low(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->wz.byte.low = cpu->bus_data;
}

static void exit_to_wz_high(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->wz.byte.high = cpu->bus_data;
}

static void exit_to_l(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->hl.byte.low = cpu->bus_data;
}

/*
 * Sending a byte out. A write hook must set both halves of the request: the
 * data bus still carries whatever the last read left there, so an enter hook
 * that names only an address would write that stale byte to memory.
 */

static void write_at(z80_t *cpu, uint16_t address, uint8_t value)
{
    cpu->bus_addr = address;
    cpu->bus_data = value;
}

static void enter_write_reg_to_hl(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    const uint8_t *source = register_slot(cpu, cpu->opcode);
    write_at(cpu, cpu->hl.word, source ? *source : 0u);
}

static void enter_write_a_at_bc(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    write_at(cpu, cpu->bc.word, cpu->af.byte.high);
}

static void enter_write_a_at_de(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    write_at(cpu, cpu->de.word, cpu->af.byte.high);
}

static void enter_write_a_at_tmp(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    write_at(cpu, cpu->tmp.word, cpu->af.byte.high);
}

static void enter_write_l_at_tmp(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    write_at(cpu, cpu->tmp.word, cpu->hl.byte.low);
}

static void enter_write_h_at_tmp(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    write_at(cpu, (uint16_t)(cpu->tmp.word + 1u), cpu->hl.byte.high);
}

static void enter_write_h_at_sp(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    write_at(cpu, (uint16_t)(cpu->sp.word + 1u), cpu->hl.byte.high);
}

static void enter_write_l_at_sp(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    write_at(cpu, cpu->sp.word, cpu->hl.byte.low);
}

/* ---------------------------------------------------------------- */
/* What each instruction does                                        */
/* ---------------------------------------------------------------- */

/**
 * HALT stops the program counter, not the clock. The core keeps running M1
 * cycles at the halt address - refreshing memory as it goes - with the HALT
 * pin asserted, until an interrupt or a reset releases it.
 */
static void execute_halt(z80_t *cpu, z80_pins_t *pins)
{
    cpu->halted = true;
    pins->ctrl |= Z80_HALT;
}

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

/** LD r,n and LD r,(HL): both end with the byte the read cycle collected. */
static void execute_ld_reg_from_bus(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    uint8_t *destination = register_slot(cpu, (uint8_t)(cpu->opcode >> 3));
    if (destination)
    {
        *destination = cpu->bus_data;
    }
}

/** WZ follows the address that was used, plus one - the MEMPTR rule. */
static void execute_ld_a_from_pair(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->af.byte.high = cpu->bus_data;
    cpu->wz.word = (uint16_t)(cpu->bus_addr + 1u);
}

/** Storing A leaves WZ half formed: low from the address, high from A. */
static void execute_store_a_memptr(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->wz.byte.low = (uint8_t)(cpu->bus_addr + 1u);
    cpu->wz.byte.high = cpu->af.byte.high;
}

static void execute_ld_rp_nn(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    pair_rp(cpu, cpu->opcode)->word = cpu->tmp.word;
}

/** LD HL,(nn): L arrived in the cycle before, H is on the bus now. */
static void execute_ld_hl_from_memory(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->hl.byte.high = cpu->bus_data;
    cpu->wz.word = (uint16_t)(cpu->tmp.word + 1u);
}

/** LD (nn),HL touches no register, but WZ still follows the address. */
static void execute_memptr_after_tmp(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->wz.word = (uint16_t)(cpu->tmp.word + 1u);
}

static void execute_ld_sp_hl(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->sp.word = cpu->hl.word;
}

static void execute_pop(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->tmp.byte.high = cpu->bus_data;
    pair_rp2(cpu, cpu->opcode)->word = cpu->tmp.word;
}

static void execute_ex_de_hl(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    const uint16_t swap = cpu->de.word;
    cpu->de.word = cpu->hl.word;
    cpu->hl.word = swap;
}

static void execute_ex_af(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    const uint16_t swap = cpu->af.word;
    cpu->af.word = cpu->af_alt.word;
    cpu->af_alt.word = swap;
}

static void execute_exx(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    uint16_t swap = cpu->bc.word;
    cpu->bc.word = cpu->bc_alt.word;
    cpu->bc_alt.word = swap;
    swap = cpu->de.word;
    cpu->de.word = cpu->de_alt.word;
    cpu->de_alt.word = swap;
    swap = cpu->hl.word;
    cpu->hl.word = cpu->hl_alt.word;
    cpu->hl_alt.word = swap;
}

static void execute_ex_sp_hl(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->hl.word = cpu->tmp.word;
    cpu->wz.word = cpu->tmp.word;
}

static void execute_alu_reg(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    const uint8_t *source = register_slot(cpu, cpu->opcode);
    if (source)
    {
        alu_operation(cpu, (uint8_t)(cpu->opcode >> 3), *source);
    }
}

static void execute_alu_bus(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    alu_operation(cpu, (uint8_t)(cpu->opcode >> 3), cpu->bus_data);
}

static void execute_inc_reg(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    uint8_t *slot = register_slot(cpu, (uint8_t)(cpu->opcode >> 3));
    if (slot)
    {
        *slot = alu_inc(cpu, *slot);
    }
}

static void execute_dec_reg(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    uint8_t *slot = register_slot(cpu, (uint8_t)(cpu->opcode >> 3));
    if (slot)
    {
        *slot = alu_dec(cpu, *slot);
    }
}

/* INC (HL) and DEC (HL) modify the byte between the read and the write. */

static void exit_inc_bus(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->bus_data = alu_inc(cpu, cpu->bus_data);
}

static void exit_dec_bus(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->bus_data = alu_dec(cpu, cpu->bus_data);
}

static void execute_inc_rp(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    z80_pair *pair = pair_rp(cpu, cpu->opcode);
    pair->word = (uint16_t)(pair->word + 1u);
}

static void execute_dec_rp(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    z80_pair *pair = pair_rp(cpu, cpu->opcode);
    pair->word = (uint16_t)(pair->word - 1u);
}

static void execute_add_hl_rp(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    alu_add16(cpu, &cpu->hl, pair_rp(cpu, cpu->opcode)->word);
}

static void execute_rotate_a(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    alu_rotate_a(cpu, (uint8_t)(cpu->opcode >> 3));
}

static void execute_daa(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    alu_daa(cpu);
}

static void execute_cpl(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    const uint8_t result = (uint8_t)~cpu->af.byte.high;
    cpu->af.byte.high = result;
    cpu->af.byte.low =
        (uint8_t)((cpu->af.byte.low & ~(uint8_t)Z80_FLAG_XY) | (result & Z80_FLAG_XY) | Z80_FLAG_H | Z80_FLAG_N);
}

/*
 * SCF and CCF take X and Y from A. On real silicon the value is A OR'd with
 * the last instruction's flag output when that instruction set flags - the Q
 * register. Phase 4 models Q; until then this is the common case.
 */

static void execute_scf(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    uint8_t flags = (uint8_t)(cpu->af.byte.low & (Z80_FLAG_S | Z80_FLAG_Z | Z80_FLAG_PV));
    flags |= (uint8_t)((cpu->af.byte.high & Z80_FLAG_XY) | Z80_FLAG_C);
    cpu->af.byte.low = flags;
}

static void execute_ccf(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    const uint8_t before = cpu->af.byte.low;
    uint8_t flags = (uint8_t)(before & (Z80_FLAG_S | Z80_FLAG_Z | Z80_FLAG_PV));
    flags |= (uint8_t)(cpu->af.byte.high & Z80_FLAG_XY);
    if (before & Z80_FLAG_C)
    {
        flags |= Z80_FLAG_H; /* the old carry is where H comes from */
    }
    else
    {
        flags |= Z80_FLAG_C;
    }
    cpu->af.byte.low = flags;
}

static void execute_di(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->iff1 = false;
    cpu->iff2 = false;
}

static void execute_ei(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    /* The one-instruction delay before interrupts are actually accepted is
       Phase 5's business, along with everything else that accepts them. */
    cpu->iff1 = true;
    cpu->iff2 = true;
}

/* jumps, calls and returns */

static void execute_jump(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->wz.byte.high = cpu->bus_data;
    cpu->pc.word = cpu->wz.word;
}

static void execute_jump_conditional(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->wz.byte.high = cpu->bus_data;
    if (condition_met(cpu, (uint8_t)(cpu->opcode >> 3)))
    {
        cpu->pc.word = cpu->wz.word;
    }
}

static void execute_jump_hl(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->pc.word = cpu->hl.word;
}

/** The idle cycle this runs in only happens when the branch is taken. */
static void enter_jump_relative(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->pc.word = (uint16_t)(cpu->pc.word + (int8_t)cpu->bus_data);
    cpu->wz.word = cpu->pc.word;
}

static void exit_jr_condition(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    /* JR uses only the first four conditions, so bits 4-3 name it */
    if (!condition_met(cpu, (uint8_t)((cpu->opcode >> 3) & 3u)))
    {
        end_instruction_here(cpu);
    }
}

static void exit_djnz_condition(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->bc.byte.high = (uint8_t)(cpu->bc.byte.high - 1u);
    if (0u == cpu->bc.byte.high)
    {
        end_instruction_here(cpu);
    }
}

static void exit_call_condition(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->wz.byte.high = cpu->bus_data;
    if (!condition_met(cpu, (uint8_t)(cpu->opcode >> 3)))
    {
        end_instruction_here(cpu);
    }
}

static void exit_take_wz(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->pc.word = cpu->wz.word;
}

static void exit_return_condition(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    if (!condition_met(cpu, (uint8_t)(cpu->opcode >> 3)))
    {
        end_instruction_here(cpu);
    }
}

/** The second half of a return: the popped byte completes WZ, then PC takes it. */
static void exit_return_to_wz_high(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->wz.byte.high = cpu->bus_data;
    cpu->pc.word = cpu->wz.word;
}

static void enter_rst_target(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->wz.word = (uint16_t)(cpu->opcode & 0x38u);
    push_at(cpu, cpu->pc.byte.high);
}

/* I/O */

static void enter_io_address(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    /* IN A,(n) and OUT (n),A put A on the top half of the address bus */
    cpu->bus_addr = (uint16_t)(((uint16_t)cpu->af.byte.high << 8) | cpu->tmp.byte.low);
}

static void enter_io_write(z80_t *cpu, z80_pins_t *pins)
{
    enter_io_address(cpu, pins);
    cpu->bus_data = cpu->af.byte.high;
}

static void execute_in_a(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->af.byte.high = cpu->bus_data;
    cpu->wz.word = (uint16_t)(cpu->bus_addr + 1u);
}

/* the CB set: shifts, rotates and single-bit work */

static void execute_cb_reg(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    const uint8_t group = (uint8_t)(cpu->opcode >> 6);
    const uint8_t bit = (uint8_t)((cpu->opcode >> 3) & 7u);
    uint8_t *slot = register_slot(cpu, cpu->opcode);

    if (!slot)
    {
        return;
    }

    switch (group)
    {
    case 0:
        *slot = alu_shift(cpu, bit, *slot);
        break;
    case 1:
        alu_bit(cpu, bit, *slot, *slot);
        break;
    case 2:
        *slot = (uint8_t)(*slot & ~(uint8_t)(1u << bit));
        break;
    default:
        *slot = (uint8_t)(*slot | (uint8_t)(1u << bit));
        break;
    }
}

/** The read-modify-write forms change the byte between the two bus cycles. */
static void exit_cb_bus(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    const uint8_t group = (uint8_t)(cpu->opcode >> 6);
    const uint8_t bit = (uint8_t)((cpu->opcode >> 3) & 7u);

    switch (group)
    {
    case 0:
        cpu->bus_data = alu_shift(cpu, bit, cpu->bus_data);
        break;
    case 2:
        cpu->bus_data = (uint8_t)(cpu->bus_data & ~(uint8_t)(1u << bit));
        break;
    default:
        cpu->bus_data = (uint8_t)(cpu->bus_data | (uint8_t)(1u << bit));
        break;
    }
}

static void execute_cb_bit_memory(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    alu_bit(cpu, (uint8_t)((cpu->opcode >> 3) & 7u), cpu->bus_data, cpu->wz.byte.high);
}

/**
 * A prefix is a whole M1 cycle that decodes nothing: it costs four T-states,
 * increments R a second time, and tells the next fetch which table to use.
 */
static void execute_prefix(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->prefix = cpu->opcode;
}

/** An opcode the core does not implement yet: costs its fetch, does nothing. */
static void execute_unimplemented(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    ++cpu->unimplemented;
}

/* ---------------------------------------------------------------- */
/* The instruction table                                             */
/* ---------------------------------------------------------------- */

#define NO_CYCLES                                                                                                      \
    {                                                                                                                  \
        {                                                                                                              \
            NULL, NULL, NULL                                                                                           \
        }                                                                                                              \
    }

/* 4 T-states: the fetch and nothing else */
static const z80_instr instr_nop = {NO_CYCLES, 0u, NULL};
static const z80_instr instr_halt = {NO_CYCLES, 0u, execute_halt};
static const z80_instr instr_ld_reg_reg = {NO_CYCLES, 0u, execute_ld_reg_reg};
static const z80_instr instr_alu_reg = {NO_CYCLES, 0u, execute_alu_reg};
static const z80_instr instr_inc_reg = {NO_CYCLES, 0u, execute_inc_reg};
static const z80_instr instr_dec_reg = {NO_CYCLES, 0u, execute_dec_reg};
static const z80_instr instr_rotate_a = {NO_CYCLES, 0u, execute_rotate_a};
static const z80_instr instr_daa = {NO_CYCLES, 0u, execute_daa};
static const z80_instr instr_cpl = {NO_CYCLES, 0u, execute_cpl};
static const z80_instr instr_scf = {NO_CYCLES, 0u, execute_scf};
static const z80_instr instr_ccf = {NO_CYCLES, 0u, execute_ccf};
static const z80_instr instr_ex_de_hl = {NO_CYCLES, 0u, execute_ex_de_hl};
static const z80_instr instr_ex_af = {NO_CYCLES, 0u, execute_ex_af};
static const z80_instr instr_exx = {NO_CYCLES, 0u, execute_exx};
static const z80_instr instr_jp_hl = {NO_CYCLES, 0u, execute_jump_hl};
static const z80_instr instr_di = {NO_CYCLES, 0u, execute_di};
static const z80_instr instr_ei = {NO_CYCLES, 0u, execute_ei};
static const z80_instr instr_unimplemented = {NO_CYCLES, 0u, execute_unimplemented};
static const z80_instr instr_prefix = {NO_CYCLES, 0u, execute_prefix};
static const z80_instr instr_cb_reg = {NO_CYCLES, 0u, execute_cb_reg};

/* CB with a memory operand: 15 T-states, of which the read is given four */
static const z80_instr instr_cb_mem = {
    {{&mem_read_seq, at_hl, exit_cb_bus}, {&idle1_seq, NULL, NULL}, {&mem_write_seq, at_hl, NULL}}, 3u, NULL};

/* BIT reads and tests but never writes back, so it is three T-states shorter */
static const z80_instr instr_cb_bit_mem = {
    {{&mem_read_seq, at_hl, NULL}, {&idle1_seq, NULL, NULL}}, 2u, execute_cb_bit_memory};

/* 8-bit loads */
static const z80_instr instr_ld_reg_immediate = {{{&mem_read_seq, at_pc, NULL}}, 1u, execute_ld_reg_from_bus};
static const z80_instr instr_ld_reg_from_hl = {{{&mem_read_seq, at_hl, NULL}}, 1u, execute_ld_reg_from_bus};
static const z80_instr instr_ld_hl_from_reg = {{{&mem_write_seq, enter_write_reg_to_hl, NULL}}, 1u, NULL};
static const z80_instr instr_ld_hl_n = {{{&mem_read_seq, at_pc, NULL}, {&mem_write_seq, at_hl, NULL}}, 2u, NULL};
static const z80_instr instr_ld_a_bc = {{{&mem_read_seq, at_bc, NULL}}, 1u, execute_ld_a_from_pair};
static const z80_instr instr_ld_a_de = {{{&mem_read_seq, at_de, NULL}}, 1u, execute_ld_a_from_pair};
static const z80_instr instr_ld_bc_a = {{{&mem_write_seq, enter_write_a_at_bc, NULL}}, 1u, execute_store_a_memptr};
static const z80_instr instr_ld_de_a = {{{&mem_write_seq, enter_write_a_at_de, NULL}}, 1u, execute_store_a_memptr};

static const z80_instr instr_ld_a_nn = {
    {{&mem_read_seq, at_pc, exit_to_tmp_low}, {&mem_read_seq, at_pc, exit_to_tmp_high}, {&mem_read_seq, at_tmp, NULL}},
    3u,
    execute_ld_a_from_pair};

static const z80_instr instr_ld_nn_a = {{{&mem_read_seq, at_pc, exit_to_tmp_low},
                                         {&mem_read_seq, at_pc, exit_to_tmp_high},
                                         {&mem_write_seq, enter_write_a_at_tmp, NULL}},
                                        3u,
                                        execute_store_a_memptr};

/* 16-bit loads */
static const z80_instr instr_ld_rp_nn = {
    {{&mem_read_seq, at_pc, exit_to_tmp_low}, {&mem_read_seq, at_pc, exit_to_tmp_high}}, 2u, execute_ld_rp_nn};

static const z80_instr instr_ld_nn_hl = {{{&mem_read_seq, at_pc, exit_to_tmp_low},
                                          {&mem_read_seq, at_pc, exit_to_tmp_high},
                                          {&mem_write_seq, enter_write_l_at_tmp, NULL},
                                          {&mem_write_seq, enter_write_h_at_tmp, NULL}},
                                         4u,
                                         execute_memptr_after_tmp};

static const z80_instr instr_ld_hl_nn = {{{&mem_read_seq, at_pc, exit_to_tmp_low},
                                          {&mem_read_seq, at_pc, exit_to_tmp_high},
                                          {&mem_read_seq, at_tmp, exit_to_l},
                                          {&mem_read_seq, at_tmp_high, NULL}},
                                         4u,
                                         execute_ld_hl_from_memory};

static const z80_instr instr_ld_sp_hl = {{{&idle2_seq, NULL, NULL}}, 1u, execute_ld_sp_hl};

static const z80_instr instr_push = {{{&idle1_seq, NULL, NULL},
                                      {&mem_write_seq, enter_push_pair_high, NULL},
                                      {&mem_write_seq, enter_push_pair_low, NULL}},
                                     3u,
                                     NULL};

static const z80_instr instr_pop = {
    {{&mem_read_seq, at_pop, exit_to_tmp_low}, {&mem_read_seq, at_pop, NULL}}, 2u, execute_pop};

/*
 * EX (SP),HL is the longest unprefixed instruction: 19 T-states. The manual
 * counts them 4,3,4,3,5 - the extra T-states are internal, after the bus cycle
 * they are attached to, so they appear here as idle cycles of their own. HL
 * must still hold the old value when the writes happen, which is why the swap
 * is in execute rather than in a hook.
 */
static const z80_instr instr_ex_sp_hl = {{{&mem_read_seq, at_sp, exit_to_tmp_low},
                                          {&mem_read_seq, at_sp_high, exit_to_tmp_high},
                                          {&idle1_seq, NULL, NULL},
                                          {&mem_write_seq, enter_write_h_at_sp, NULL},
                                          {&mem_write_seq, enter_write_l_at_sp, NULL},
                                          {&idle2_seq, NULL, NULL}},
                                         6u,
                                         execute_ex_sp_hl};

/* arithmetic */
static const z80_instr instr_alu_mem = {{{&mem_read_seq, at_hl, NULL}}, 1u, execute_alu_bus};
static const z80_instr instr_alu_n = {{{&mem_read_seq, at_pc, NULL}}, 1u, execute_alu_bus};

static const z80_instr instr_inc_mem = {
    {{&mem_read_seq, at_hl, exit_inc_bus}, {&idle1_seq, NULL, NULL}, {&mem_write_seq, at_hl, NULL}}, 3u, NULL};

static const z80_instr instr_dec_mem = {
    {{&mem_read_seq, at_hl, exit_dec_bus}, {&idle1_seq, NULL, NULL}, {&mem_write_seq, at_hl, NULL}}, 3u, NULL};

static const z80_instr instr_inc_rp = {{{&idle2_seq, NULL, NULL}}, 1u, execute_inc_rp};
static const z80_instr instr_dec_rp = {{{&idle2_seq, NULL, NULL}}, 1u, execute_dec_rp};
static const z80_instr instr_add_hl_rp = {{{&idle7_seq, NULL, NULL}}, 1u, execute_add_hl_rp};

/* control flow */
static const z80_instr instr_jump = {
    {{&mem_read_seq, at_pc, exit_to_wz_low}, {&mem_read_seq, at_pc, NULL}}, 2u, execute_jump};

static const z80_instr instr_jp_cc = {
    {{&mem_read_seq, at_pc, exit_to_wz_low}, {&mem_read_seq, at_pc, NULL}}, 2u, execute_jump_conditional};

static const z80_instr instr_jr = {{{&mem_read_seq, at_pc, NULL}, {&idle5_seq, enter_jump_relative, NULL}}, 2u, NULL};

static const z80_instr instr_jr_cc = {
    {{&mem_read_seq, at_pc, exit_jr_condition}, {&idle5_seq, enter_jump_relative, NULL}}, 2u, NULL};

static const z80_instr instr_djnz = {
    {{&idle1_seq, NULL, NULL}, {&mem_read_seq, at_pc, exit_djnz_condition}, {&idle5_seq, enter_jump_relative, NULL}},
    3u,
    NULL};

static const z80_instr instr_call = {{{&mem_read_seq, at_pc, exit_to_wz_low},
                                      {&mem_read_seq, at_pc, exit_to_wz_high},
                                      {&idle1_seq, NULL, NULL},
                                      {&mem_write_seq, enter_push_pc_high, NULL},
                                      {&mem_write_seq, enter_push_pc_low, exit_take_wz}},
                                     5u,
                                     NULL};

static const z80_instr instr_call_cc = {{{&mem_read_seq, at_pc, exit_to_wz_low},
                                         {&mem_read_seq, at_pc, exit_call_condition},
                                         {&idle1_seq, NULL, NULL},
                                         {&mem_write_seq, enter_push_pc_high, NULL},
                                         {&mem_write_seq, enter_push_pc_low, exit_take_wz}},
                                        5u,
                                        NULL};

static const z80_instr instr_ret = {
    {{&mem_read_seq, at_pop, exit_to_wz_low}, {&mem_read_seq, at_pop, exit_return_to_wz_high}}, 2u, NULL};

static const z80_instr instr_ret_cc = {{{&idle1_seq, NULL, exit_return_condition},
                                        {&mem_read_seq, at_pop, exit_to_wz_low},
                                        {&mem_read_seq, at_pop, exit_return_to_wz_high}},
                                       3u,
                                       NULL};

static const z80_instr instr_rst = {{{&idle1_seq, NULL, NULL},
                                     {&mem_write_seq, enter_rst_target, NULL},
                                     {&mem_write_seq, enter_push_pc_low, exit_take_wz}},
                                    3u,
                                    NULL};

/* I/O */
static const z80_instr instr_in_a_n = {
    {{&mem_read_seq, at_pc, exit_to_tmp_low}, {&io_read_seq, enter_io_address, NULL}}, 2u, execute_in_a};

static const z80_instr instr_out_n_a = {
    {{&mem_read_seq, at_pc, exit_to_tmp_low}, {&io_write_seq, enter_io_write, NULL}}, 2u, execute_store_a_memptr};

/**
 * The CB table is the regular one: bits 7-6 pick the group, and within each
 * group bits 2-0 pick the operand exactly as they do unprefixed. Only whether
 * the operand is (HL), and whether the group writes back, changes the timing.
 */
static const z80_instr *decode_cb(const z80_t *cpu)
{
    if (6u != (cpu->opcode & 7u))
    {
        return &instr_cb_reg;
    }
    return (1u == (cpu->opcode >> 6)) ? &instr_cb_bit_mem : &instr_cb_mem;
}

/**
 * @brief Pick what runs after a fetch.
 *
 * The opcode's bit fields do the work, as they do on the real part. The names
 * are the conventional ones: x is bits 7-6, y bits 5-3, z bits 2-0, and y
 * splits again into p (bits 5-4) and q (bit 3).
 */
static const z80_instr *decode_base(const z80_t *cpu)
{
    const uint8_t opcode = cpu->opcode;
    const uint8_t x = (uint8_t)(opcode >> 6);
    const uint8_t y = (uint8_t)((opcode >> 3) & 7u);
    const uint8_t z = (uint8_t)(opcode & 7u);
    const uint8_t p = (uint8_t)(y >> 1);
    const uint8_t q = (uint8_t)(y & 1u);

    switch (x)
    {
    case 0:
        switch (z)
        {
        case 0:
            switch (y)
            {
            case 0:
                return &instr_nop;
            case 1:
                return &instr_ex_af;
            case 2:
                return &instr_djnz;
            case 3:
                return &instr_jr;
            default:
                return &instr_jr_cc;
            }
        case 1:
            return q ? &instr_add_hl_rp : &instr_ld_rp_nn;
        case 2:
            if (0u == q)
            {
                switch (p)
                {
                case 0:
                    return &instr_ld_bc_a;
                case 1:
                    return &instr_ld_de_a;
                case 2:
                    return &instr_ld_nn_hl;
                default:
                    return &instr_ld_nn_a;
                }
            }
            switch (p)
            {
            case 0:
                return &instr_ld_a_bc;
            case 1:
                return &instr_ld_a_de;
            case 2:
                return &instr_ld_hl_nn;
            default:
                return &instr_ld_a_nn;
            }
        case 3:
            return q ? &instr_dec_rp : &instr_inc_rp;
        case 4:
            return (6u == y) ? &instr_inc_mem : &instr_inc_reg;
        case 5:
            return (6u == y) ? &instr_dec_mem : &instr_dec_reg;
        case 6:
            return (6u == y) ? &instr_ld_hl_n : &instr_ld_reg_immediate;
        default:
            switch (y)
            {
            case 4:
                return &instr_daa;
            case 5:
                return &instr_cpl;
            case 6:
                return &instr_scf;
            case 7:
                return &instr_ccf;
            default:
                return &instr_rotate_a;
            }
        }

    case 1:
        if (6u == y && 6u == z)
        {
            return &instr_halt; /* the hole in the LD r,r' block */
        }
        if (6u == z)
        {
            return &instr_ld_reg_from_hl;
        }
        if (6u == y)
        {
            return &instr_ld_hl_from_reg;
        }
        return &instr_ld_reg_reg;

    case 2:
        return (6u == z) ? &instr_alu_mem : &instr_alu_reg;

    default:
        switch (z)
        {
        case 0:
            return &instr_ret_cc;
        case 1:
            if (0u == q)
            {
                return &instr_pop;
            }
            switch (p)
            {
            case 0:
                return &instr_ret;
            case 1:
                return &instr_exx;
            case 2:
                return &instr_jp_hl;
            default:
                return &instr_ld_sp_hl;
            }
        case 2:
            return &instr_jp_cc;
        case 3:
            switch (y)
            {
            case 0:
                return &instr_jump;
            case 2:
                return &instr_out_n_a;
            case 3:
                return &instr_in_a_n;
            case 4:
                return &instr_ex_sp_hl;
            case 5:
                return &instr_ex_de_hl;
            case 6:
                return &instr_di;
            case 7:
                return &instr_ei;
            default:
                return &instr_prefix; /* CB */
            }
        case 4:
            return &instr_call_cc;
        case 5:
            if (0u == q)
            {
                return &instr_push;
            }
            if (0u == p)
            {
                return &instr_call;
            }
            return &instr_unimplemented; /* DD, ED and FD prefixes */
        case 6:
            return &instr_alu_n;
        default:
            return &instr_rst;
        }
    }
}

/** Which table the byte just fetched should be read against. */
static const z80_instr *decode(const z80_t *cpu)
{
    if (0xCBu == cpu->prefix)
    {
        return decode_cb(cpu);
    }
    return decode_base(cpu);
}

/* ---------------------------------------------------------------- */
/* Lifetime                                                          */
/* ---------------------------------------------------------------- */

const char *z80_version(void)
{
    return Z80CORE_VERSION;
}

z80_t *z80_new(void)
{
    z80_t *cpu = (z80_t *)malloc(sizeof *cpu);
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
        const bool was_fetch = (cpu->seq == &m1_fetch_seq);

        if (!was_fetch && cpu->instr && cpu->instr->cycles[cpu->cycle].exit)
        {
            cpu->instr->cycles[cpu->cycle].exit(cpu, pins);
        }

        if (was_fetch)
        {
            /* the fetch just ended: start the instruction it decoded */
            cpu->instr = decode(cpu);
            cpu->cycle = 0;
            cpu->cycle_limit = cpu->instr->cycle_count;
        }
        else
        {
            ++cpu->cycle;
        }

        cpu->step = 0;

        if (cpu->instr && cpu->cycle < cpu->cycle_limit)
        {
            const z80_cycle *next = &cpu->instr->cycles[cpu->cycle];
            cpu->seq = next->seq;
            if (next->enter)
            {
                next->enter(cpu, pins);
            }
        }
        else
        {
            /* every cycle is done, so the instruction takes effect and the
               next fetch begins */
            if (cpu->instr && cpu->instr->execute)
            {
                cpu->instr->execute(cpu, pins);
            }
            /* A prefix has just set this; anything else has finished with it. */
            if (cpu->instr != &instr_prefix)
            {
                cpu->prefix = 0;
            }
            cpu->instr = NULL;
            cpu->seq = &m1_fetch_seq;
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
    uint8_t cycle;       /**< which of the instruction's cycles, when not fetching */
    uint8_t cycle_limit; /**< how many it will run; a condition may have lowered it */
    uint8_t opcode;
    uint8_t prefix;
    uint8_t clk;
    uint16_t bus_addr;
    uint8_t bus_data;
    uint16_t tmp;
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
    snapshot.cycle_limit = cpu->cycle_limit;
    snapshot.opcode = cpu->opcode;
    snapshot.prefix = cpu->prefix;
    snapshot.clk = cpu->clk;
    snapshot.bus_addr = cpu->bus_addr;
    snapshot.bus_data = cpu->bus_data;
    snapshot.tmp = cpu->tmp.word;
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
    cpu->prefix = snapshot.prefix;
    cpu->clk = snapshot.clk;
    cpu->edges = snapshot.edges;
    cpu->unimplemented = snapshot.unimplemented;

    cpu->bus_addr = snapshot.bus_addr;
    cpu->bus_data = snapshot.bus_data;
    cpu->tmp.word = snapshot.tmp;

    /* Restore where in the instruction it was. The sequence is not stored as a
       pointer - that would not survive a different build - but rebuilt from
       the opcode and the cycle index, which describe the same position. */
    if (snapshot.in_fetch)
    {
        cpu->instr = NULL;
        cpu->cycle = 0;
        cpu->cycle_limit = 0;
        cpu->seq = &m1_fetch_seq;
    }
    else
    {
        cpu->instr = decode(cpu);
        cpu->cycle = snapshot.cycle;
        cpu->cycle_limit = snapshot.cycle_limit;
        if (cpu->instr && cpu->cycle < cpu->cycle_limit && cpu->cycle < Z80_MAX_CYCLES &&
            cpu->instr->cycles[cpu->cycle].seq)
        {
            cpu->seq = cpu->instr->cycles[cpu->cycle].seq;
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
