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
 * PHASE 3 is complete: every encoding of the instruction set, prefixed and
 * not, along with the ALU and its flags and the I/O cycles. Nothing reaches
 * z80_unimplemented() any more, which is what finishing the set means.
 *
 * What is still owed: the Q register behind SCF and CCF, and the conformance
 * suites that would prove the undocumented corners rather than assert them
 * (Phase 4); interrupts, and with them the only two behaviours the instruction
 * set cannot express by itself - the delay after EI and waking from HALT
 * (Phase 5).
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
#define Z80_SNAPSHOT_VERSION 6u

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
    uint8_t prefix;            /**< CB or ED in force, or 0 */
    uint8_t index;             /**< DD or FD in force, or 0 */
    uint8_t clk;               /**< the level the core last advanced to */

    /* the instruction in progress, and the bus cycle it is running */
    const struct z80_instr *instr;
    uint8_t cycle;       /**< which of the instruction's cycles */
    uint8_t cycle_limit; /**< how many it will actually run; a condition may lower it */
    uint16_t bus_addr;   /**< address the current read or write uses */
    uint8_t bus_data;    /**< byte read, or byte to write */
    z80_pair tmp;        /**< 16-bit scratch, for operands WZ must not see */

    /* interrupts */
    bool nmi_previous;  /**< the level NMI was at last edge, for edge detection */
    bool nmi_latched;   /**< an NMI edge is pending acceptance */
    bool int_inhibit;   /**< EI ran last: do not accept before the next boundary */
    uint8_t reset_held; /**< T-states RESET has been asserted for */

    /* bus arbitration */
    bool bus_request_latched; /**< BUSRQ seen at this cycle's sampling edge */
    bool bus_released;        /**< the host has the buses; BUSAK is asserted */

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

    cpu->pc.word = (uint16_t)(cpu->pc.word + 1u);

    pins->ctrl &= ~(uint32_t)(Z80_M1 | Z80_MREQ | Z80_RD);

    /* Refresh address out, RFSH asserted, and only then R incremented. The
       address carries the value R had for this fetch, not the one the next
       fetch will use - putting the increment first drives every refresh one
       row further into the DRAM than the part does. Bit 7 is not part of the
       counter and survives untouched. */
    pins->A = (uint16_t)(((uint16_t)cpu->i << 8) | cpu->r);
    cpu->r = (uint8_t)((cpu->r & 0x80u) | ((cpu->r + 1u) & 0x7Fu));
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
static const z80_seq idle4_seq = {idle_steps, 8u, Z80_NO_WAIT};
static const z80_seq idle5_seq = {idle_steps, 10u, Z80_NO_WAIT};
static const z80_seq idle7_seq = {idle_steps, 14u, Z80_NO_WAIT};

/*
 * The two acknowledge cycles.
 *
 * An interrupt acknowledge is seven T-states: an M1 with IORQ alongside it -
 * the one combination no other cycle uses - and two automatic wait states so a
 * peripheral has time to put a vector on the bus. Seven is what makes the
 * published totals work: mode 1 is 7+3+3 = 13, mode 2 is 7+3+3+3+3 = 19.
 *
 * An NMI acknowledge is five, with no IORQ. It performs an ordinary fetch and
 * throws the byte away, and PC does not move - the vector is fixed.
 */

static void step_int_ack_start(z80_t *cpu, z80_pins_t *pins)
{
    pins->A = cpu->pc.word;
    pins->ctrl |= Z80_M1;
}

static void step_int_ack_iorq(z80_t *cpu, z80_pins_t *pins)
{
    (void)cpu;
    pins->ctrl |= Z80_IORQ;
}

/** Take the vector, end the acknowledge, and refresh as any M1 does. */
static void step_int_ack_sample(z80_t *cpu, z80_pins_t *pins)
{
    cpu->bus_data = pins->D;
    pins->ctrl &= ~(uint32_t)(Z80_M1 | Z80_IORQ);

    pins->A = (uint16_t)(((uint16_t)cpu->i << 8) | cpu->r);
    cpu->r = (uint8_t)((cpu->r & 0x80u) | ((cpu->r + 1u) & 0x7Fu));
    pins->ctrl |= Z80_RFSH;
}

/** The NMI fetch discards its byte and leaves PC alone. */
static void step_nmi_ack_discard(z80_t *cpu, z80_pins_t *pins)
{
    pins->ctrl &= ~(uint32_t)(Z80_M1 | Z80_MREQ | Z80_RD);

    pins->A = (uint16_t)(((uint16_t)cpu->i << 8) | cpu->r);
    cpu->r = (uint8_t)((cpu->r & 0x80u) | ((cpu->r + 1u) & 0x7Fu));
    pins->ctrl |= Z80_RFSH;
}

static const z80_step_fn int_ack_steps[] = {
    step_int_ack_start,  /* T1 rise: address out, M1 asserted       */
    step_nothing,        /* T1 fall                                 */
    step_int_ack_iorq,   /* T2 rise: IORQ alongside M1              */
    step_nothing,        /* T2 fall: WAIT sampled here              */
    step_nothing,        /* TW rise: the first automatic wait       */
    step_nothing,        /* TW fall                                 */
    step_nothing,        /* TW rise: the second                     */
    step_nothing,        /* TW fall                                 */
    step_int_ack_sample, /* T3 rise: vector taken, refresh begins   */
    step_m1_t3_fall,     /* T3 fall: MREQ for the refresh           */
    step_nothing,        /* T4 rise                                 */
    step_m1_t4_fall,     /* T4 fall: MREQ and RFSH released         */
    step_nothing,        /* T5 rise                                 */
    step_nothing         /* T5 fall                                 */
};

static const z80_step_fn nmi_ack_steps[] = {
    step_m1_t1_rise,      /* T1 rise: address out, M1 asserted    */
    step_m1_t1_fall,      /* T1 fall: MREQ, RD asserted           */
    step_nothing,         /* T2 rise                              */
    step_nothing,         /* T2 fall: WAIT sampled here           */
    step_nmi_ack_discard, /* T3 rise: byte discarded, PC unmoved  */
    step_m1_t3_fall,      /* T3 fall: MREQ for the refresh        */
    step_nothing,         /* T4 rise                              */
    step_m1_t4_fall,      /* T4 fall: MREQ and RFSH released      */
    step_nothing,         /* T5 rise                              */
    step_nothing          /* T5 fall                              */
};

static const z80_seq int_ack_seq = {int_ack_steps, 14u, 3u};
static const z80_seq nmi_ack_seq = {nmi_ack_steps, 10u, 3u};

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

/**
 * ADC HL,rr and SBC HL,rr, which unlike ADD HL,rr set every flag: they are
 * meant for extended-precision arithmetic, so S, Z and overflow all matter.
 */
static void alu_adc16(z80_t *cpu, uint16_t value)
{
    const uint16_t a = cpu->hl.word;
    const uint8_t carry = (uint8_t)((cpu->af.byte.low & Z80_FLAG_C) ? 1u : 0u);
    const uint32_t wide = (uint32_t)a + value + carry;
    const uint16_t result = (uint16_t)wide;

    uint8_t flags = (uint8_t)((result >> 8) & (Z80_FLAG_S | Z80_FLAG_XY));
    if (0u == result)
    {
        flags |= Z80_FLAG_Z;
    }
    if ((a ^ value ^ result) & 0x1000u)
    {
        flags |= Z80_FLAG_H;
    }
    if ((a ^ result) & (value ^ result) & 0x8000u)
    {
        flags |= Z80_FLAG_PV;
    }
    if (wide & 0x10000u)
    {
        flags |= Z80_FLAG_C;
    }

    cpu->wz.word = (uint16_t)(a + 1u);
    cpu->hl.word = result;
    cpu->af.byte.low = flags;
}

static void alu_sbc16(z80_t *cpu, uint16_t value)
{
    const uint16_t a = cpu->hl.word;
    const uint8_t carry = (uint8_t)((cpu->af.byte.low & Z80_FLAG_C) ? 1u : 0u);
    const uint32_t wide = (uint32_t)((uint32_t)a - value - carry);
    const uint16_t result = (uint16_t)wide;

    uint8_t flags = (uint8_t)(((result >> 8) & (Z80_FLAG_S | Z80_FLAG_XY)) | Z80_FLAG_N);
    if (0u == result)
    {
        flags |= Z80_FLAG_Z;
    }
    if ((a ^ value ^ result) & 0x1000u)
    {
        flags |= Z80_FLAG_H;
    }
    if ((a ^ value) & (a ^ result) & 0x8000u)
    {
        flags |= Z80_FLAG_PV;
    }
    if (wide & 0x10000u)
    {
        flags |= Z80_FLAG_C;
    }

    cpu->wz.word = (uint16_t)(a + 1u);
    cpu->hl.word = result;
    cpu->af.byte.low = flags;
}

/*
 * The block instructions have the strangest flags on the part. X and Y do not
 * copy the result - there is no result - but bits 3 and 1 of a value computed
 * from the byte that moved. They are documented by reverse engineering rather
 * than by Zilog, and z80test is what proves them right.
 */

static void block_transfer_flags(z80_t *cpu, uint8_t value)
{
    const uint8_t n = (uint8_t)(cpu->af.byte.high + value);

    uint8_t flags = (uint8_t)(cpu->af.byte.low & (Z80_FLAG_S | Z80_FLAG_Z | Z80_FLAG_C));
    flags |= (uint8_t)(n & Z80_FLAG_X);
    if (n & 0x02u)
    {
        flags |= Z80_FLAG_Y; /* bit 1, not bit 5 */
    }
    if (0u != cpu->bc.word)
    {
        flags |= Z80_FLAG_PV; /* P/V here means "more to do" */
    }

    cpu->af.byte.low = flags;
}

static void block_compare_flags(z80_t *cpu, uint8_t value)
{
    const uint8_t a = cpu->af.byte.high;
    const uint8_t result = (uint8_t)(a - value);
    uint8_t n = result;

    uint8_t flags = (uint8_t)((cpu->af.byte.low & Z80_FLAG_C) | Z80_FLAG_N);
    flags |= (uint8_t)(result & Z80_FLAG_S);
    if (0u == result)
    {
        flags |= Z80_FLAG_Z;
    }
    if ((a ^ value ^ result) & 0x10u)
    {
        flags |= Z80_FLAG_H;
        n = (uint8_t)(n - 1u);
    }
    flags |= (uint8_t)(n & Z80_FLAG_X);
    if (n & 0x02u)
    {
        flags |= Z80_FLAG_Y;
    }
    if (0u != cpu->bc.word)
    {
        flags |= Z80_FLAG_PV;
    }

    cpu->af.byte.low = flags;
}

static void block_io_flags(z80_t *cpu, uint8_t value, uint8_t addend)
{
    const uint16_t k = (uint16_t)(value + addend);

    uint8_t flags = flags_sz_xy(cpu->bc.byte.high);
    if (value & 0x80u)
    {
        flags |= Z80_FLAG_N; /* N copies bit 7 of the byte that moved */
    }
    if (k > 0xFFu)
    {
        flags |= (uint8_t)(Z80_FLAG_H | Z80_FLAG_C);
    }
    if (parity_even((uint8_t)((k & 7u) ^ cpu->bc.byte.high)))
    {
        flags |= Z80_FLAG_PV;
    }

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
 * HL, or whichever index register a DD or FD prefix has put in its place.
 *
 * The substitution is the whole of what those prefixes do: the instruction
 * decodes exactly as it would have, and every place that would have reached
 * for HL reaches here instead.
 */
static z80_pair *index_pair(z80_t *cpu)
{
    switch (cpu->index)
    {
    case 0xDDu:
        return &cpu->ix;
    case 0xFDu:
        return &cpu->iy;
    default:
        return &cpu->hl;
    }
}

/**
 * The eight places the register field of an opcode can name. Index 6 is (HL),
 * which is memory rather than a register: an instruction naming it does a bus
 * cycle, which is why those forms cost more T-states.
 *
 * @param substitute Whether a DD or FD prefix reaches the halves of the pair.
 *        It does for LD IXH,n and its neighbours, and it does not when the
 *        other operand is (IX+d) - LD H,(IX+d) really does mean H. One byte of
 *        an instruction cannot name both an index register and a plain one.
 */
static uint8_t *register_half(z80_t *cpu, uint8_t which, bool substitute)
{
    z80_pair *const high_low = substitute ? index_pair(cpu) : &cpu->hl;

    switch (which & 7u)
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
        return &high_low->byte.high; /* H, IXH or IYH */
    case 5:
        return &high_low->byte.low; /* L, IXL or IYL */
    case 7:
        return &cpu->af.byte.high; /* A */
    default:
        return NULL; /* 6 is (HL) */
    }
}

static uint8_t *register_slot(z80_t *cpu, uint8_t which)
{
    return register_half(cpu, which, true);
}

static uint8_t *register_slot_plain(z80_t *cpu, uint8_t which)
{
    return register_half(cpu, which, false);
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
        return index_pair(cpu);
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
        return index_pair(cpu);
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
    index_pair(cpu)->byte.low = cpu->bus_data;
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
    write_at(cpu, cpu->tmp.word, index_pair(cpu)->byte.low);
}

static void enter_write_h_at_tmp(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    write_at(cpu, (uint16_t)(cpu->tmp.word + 1u), index_pair(cpu)->byte.high);
}

static void enter_write_h_at_sp(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    write_at(cpu, (uint16_t)(cpu->sp.word + 1u), index_pair(cpu)->byte.high);
}

static void enter_write_l_at_sp(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    write_at(cpu, cpu->sp.word, index_pair(cpu)->byte.low);
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

    /*
     * Wind PC back onto the HALT so the next fetch reads it again. A halted
     * Z80 is not idle - it runs M1 cycles at the halt address, refreshing
     * memory as it goes - and the instruction it keeps fetching is this one.
     * Leaving PC past it would re-fetch whatever follows the HALT, which is
     * both wrong on the bus and wrong as a return address when an interrupt
     * eventually wakes it.
     */
    cpu->pc.word = (uint16_t)(cpu->pc.word - 1u);
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
    index_pair(cpu)->byte.high = cpu->bus_data;
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
    cpu->sp.word = index_pair(cpu)->word;
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
    index_pair(cpu)->word = cpu->tmp.word;
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
    alu_add16(cpu, index_pair(cpu), pair_rp(cpu, cpu->opcode)->word);
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
    cpu->iff1 = true;
    cpu->iff2 = true;
    /* and not accepted until the instruction after this one has run */
    cpu->int_inhibit = true;
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
    cpu->pc.word = index_pair(cpu)->word;
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

/**
 * BIT n,(HL) keeps no result, so X and Y come from the high half of WZ - the
 * same internal register the indexed form leaves holding IX+d. That makes the
 * two forms one rule rather than two.
 *
 * FUSE disagrees and expects them to follow the byte tested. It cannot be
 * right: FUSE has no MEMPTR in its model or its format, so WZ is zero in every
 * one of its cases and the two rules are indistinguishable there. The
 * SingleStepTests corpus varies WZ and pins this to the high half of it in
 * 400 samples out of 400. See tests/fuse/README.md.
 */
static void execute_cb_bit_memory(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    alu_bit(cpu, (uint8_t)((cpu->opcode >> 3) & 7u), cpu->bus_data, cpu->wz.byte.high);
}

/* the ED set */

static void execute_neg(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    uint8_t result = 0u;
    const uint8_t flags = sub_flags(0u, cpu->af.byte.high, 0u, &result);
    cpu->af.byte.high = result;
    cpu->af.byte.low = flags;
}

static void execute_adc_hl(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    alu_adc16(cpu, pair_rp(cpu, cpu->opcode)->word);
}

static void execute_sbc_hl(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    alu_sbc16(cpu, pair_rp(cpu, cpu->opcode)->word);
}

static void execute_set_interrupt_mode(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    /* Eight encodings, four modes: the table repeats, and two of the eight
       select mode 0 by a route Zilog never documented. */
    static const uint8_t modes[8] = {0u, 0u, 1u, 2u, 0u, 0u, 1u, 2u};
    cpu->im = modes[(cpu->opcode >> 3) & 7u];
}

static void execute_ld_i_from_a(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->i = cpu->af.byte.high;
}

static void execute_ld_r_from_a(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->r = cpu->af.byte.high;
}

/**
 * LD A,I and LD A,R put the interrupt enable flip-flop into P/V, which is the
 * only way to read it. On the real part an interrupt arriving during the
 * instruction clears it - a race Phase 5 has to model.
 */
static void load_a_from_control_register(z80_t *cpu, z80_pins_t *pins, uint8_t value)
{
    uint8_t flags = (uint8_t)(cpu->af.byte.low & Z80_FLAG_C);
    flags |= flags_sz_xy(value);

    /*
     * The race. If an interrupt is accepted while this instruction is running,
     * the flip-flop is cleared before P/V samples it, and the program reads
     * back "interrupts were off" when they were on.
     *
     * That is a real fault on real hardware, not a curiosity: code that uses
     * LD A,I to save the interrupt state around a critical section restores
     * the wrong one, and the bug lands somewhere else entirely.
     */
    const bool interrupt_imminent =
        !cpu->int_inhibit && (cpu->nmi_latched || (cpu->iff1 && 0u != (pins->ctrl & Z80_INT)));

    if (cpu->iff2 && !interrupt_imminent)
    {
        flags |= Z80_FLAG_PV;
    }

    cpu->af.byte.high = value;
    cpu->af.byte.low = flags;
}

static void execute_ld_a_from_i(z80_t *cpu, z80_pins_t *pins)
{
    load_a_from_control_register(cpu, pins, cpu->i);
}

static void execute_ld_a_from_r(z80_t *cpu, z80_pins_t *pins)
{
    load_a_from_control_register(cpu, pins, cpu->r);
}

static void exit_return_from_interrupt(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->wz.byte.high = cpu->bus_data;
    cpu->pc.word = cpu->wz.word;
    cpu->iff1 = cpu->iff2; /* what makes this a return from an interrupt */
}

/* RRD and RLD rotate one BCD digit between A and memory. */

static void digit_rotate_flags(z80_t *cpu)
{
    uint8_t flags = (uint8_t)(cpu->af.byte.low & Z80_FLAG_C);
    flags |= flags_sz_xy(cpu->af.byte.high);
    if (parity_even(cpu->af.byte.high))
    {
        flags |= Z80_FLAG_PV;
    }
    cpu->af.byte.low = flags;
    cpu->wz.word = (uint16_t)(cpu->hl.word + 1u);
}

static void exit_rrd(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    const uint8_t a = cpu->af.byte.high;
    const uint8_t memory = cpu->bus_data;

    cpu->bus_data = (uint8_t)((a << 4) | (memory >> 4));
    cpu->af.byte.high = (uint8_t)((a & 0xF0u) | (memory & 0x0Fu));
    digit_rotate_flags(cpu);
}

static void exit_rld(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    const uint8_t a = cpu->af.byte.high;
    const uint8_t memory = cpu->bus_data;

    cpu->bus_data = (uint8_t)((memory << 4) | (a & 0x0Fu));
    cpu->af.byte.high = (uint8_t)((a & 0xF0u) | (memory >> 4));
    digit_rotate_flags(cpu);
}

/* IN r,(C) and OUT (C),r address the port with the whole of BC. */

static void enter_io_at_bc(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->bus_addr = cpu->bc.word;
}

static void enter_out_c(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    /* register_slot gives NULL for index 6, which is the encoding that outputs
       a constant - zero on the NMOS part */
    const uint8_t *source = register_slot(cpu, (uint8_t)(cpu->opcode >> 3));
    write_at(cpu, cpu->bc.word, source ? *source : 0u);
}

static void execute_in_c(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    const uint8_t value = cpu->bus_data;
    uint8_t *slot = register_slot(cpu, (uint8_t)(cpu->opcode >> 3));

    if (slot)
    {
        *slot = value; /* index 6 is IN (C): the flags move, nothing else */
    }

    uint8_t flags = (uint8_t)(cpu->af.byte.low & Z80_FLAG_C);
    flags |= flags_sz_xy(value);
    if (parity_even(value))
    {
        flags |= Z80_FLAG_PV;
    }
    cpu->af.byte.low = flags;
    cpu->wz.word = (uint16_t)(cpu->bc.word + 1u);
}

static void execute_out_c(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->wz.word = (uint16_t)(cpu->bc.word + 1u);
}

/* LD (nn),rr and LD rr,(nn), the ED forms that reach every pair */

static void enter_write_rp_low(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    write_at(cpu, cpu->tmp.word, pair_rp(cpu, cpu->opcode)->byte.low);
}

static void enter_write_rp_high(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    write_at(cpu, (uint16_t)(cpu->tmp.word + 1u), pair_rp(cpu, cpu->opcode)->byte.high);
}

static void exit_to_rp_low(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    pair_rp(cpu, cpu->opcode)->byte.low = cpu->bus_data;
}

static void execute_ld_rp_from_memory(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    pair_rp(cpu, cpu->opcode)->byte.high = cpu->bus_data;
    cpu->wz.word = (uint16_t)(cpu->tmp.word + 1u);
}

/*
 * The block instructions. One descriptor serves all four of each family: bit 3
 * of the opcode chooses the direction and bit 4 whether it repeats, exactly as
 * the encoding intends, so LDI and LDDR differ only in what the hooks read out
 * of the opcode they are already holding.
 *
 * Repeating is done the way the hardware does it - by winding PC back over the
 * two prefix bytes so the same instruction is fetched again. An interrupt can
 * therefore be taken between iterations, which is the entire reason a 64 KB
 * copy does not lock the machine out for the duration.
 */

/**
 * A repeating block instruction takes X and Y from the high half of the
 * address it is about to resume at - its own, since PC is wound back over the
 * two prefix bytes to fetch it again.
 *
 * That is not a special case bolted on: A-Z80 shows X and Y are never derived
 * from a result at all, but copied from bits 3 and 5 of whatever byte is on
 * the internal data bus when the flags latch. During the rewind that byte is
 * the program counter, not the one that moved.
 */
static void block_repeat_undocumented(z80_t *cpu)
{
    const uint8_t high = (uint8_t)((uint16_t)(cpu->pc.word - 2u) >> 8);
    cpu->af.byte.low = (uint8_t)((cpu->af.byte.low & ~(uint8_t)Z80_FLAG_XY) | (high & Z80_FLAG_XY));
}

static int block_delta(const z80_t *cpu)
{
    return (cpu->opcode & 0x08u) ? -1 : 1;
}

static bool block_repeats(const z80_t *cpu)
{
    return 0u != (cpu->opcode & 0x10u);
}

static void enter_block_repeat(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->pc.word = (uint16_t)(cpu->pc.word - 2u);
    cpu->wz.word = (uint16_t)(cpu->pc.word + 1u);
}

/** The write of a transfer: the byte is already on the bus from the read. */
static void enter_block_write_de(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->bus_addr = cpu->de.word;
}

static void enter_block_write_hl(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->bus_addr = cpu->hl.word;
}

static void exit_block_transfer(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    const int delta = block_delta(cpu);
    const uint8_t value = cpu->bus_data;

    cpu->hl.word = (uint16_t)(cpu->hl.word + delta);
    cpu->de.word = (uint16_t)(cpu->de.word + delta);
    cpu->bc.word = (uint16_t)(cpu->bc.word - 1u);
    block_transfer_flags(cpu, value);

    if (!block_repeats(cpu) || 0u == cpu->bc.word)
    {
        end_instruction_here(cpu);
    }
    else
    {
        block_repeat_undocumented(cpu);
    }
}

static void exit_block_compare(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    const int delta = block_delta(cpu);
    const uint8_t value = cpu->bus_data;

    cpu->hl.word = (uint16_t)(cpu->hl.word + delta);
    cpu->bc.word = (uint16_t)(cpu->bc.word - 1u);
    cpu->wz.word = (uint16_t)(cpu->wz.word + delta);
    block_compare_flags(cpu, value);

    /* a search stops on a match as well as on running out */
    if (!block_repeats(cpu) || 0u == cpu->bc.word || (cpu->af.byte.low & Z80_FLAG_Z))
    {
        end_instruction_here(cpu);
    }
    else
    {
        block_repeat_undocumented(cpu);
    }
}

static void exit_block_in(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    const int delta = block_delta(cpu);
    const uint8_t value = cpu->bus_data;

    cpu->wz.word = (uint16_t)(cpu->bc.word + delta);
    cpu->bc.byte.high = (uint8_t)(cpu->bc.byte.high - 1u);
    cpu->hl.word = (uint16_t)(cpu->hl.word + delta);
    block_io_flags(cpu, value, (uint8_t)(cpu->bc.byte.low + delta));

    if (!block_repeats(cpu) || 0u == cpu->bc.byte.high)
    {
        end_instruction_here(cpu);
    }
    else
    {
        block_repeat_undocumented(cpu);
    }
}

/** B is decremented before the cycle, so the port address carries the new B. */
static void enter_block_out(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->bc.byte.high = (uint8_t)(cpu->bc.byte.high - 1u);
    cpu->bus_addr = cpu->bc.word;
}

static void exit_block_out(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    const int delta = block_delta(cpu);
    const uint8_t value = cpu->bus_data;

    cpu->hl.word = (uint16_t)(cpu->hl.word + delta);
    cpu->wz.word = (uint16_t)(cpu->bc.word + delta);
    block_io_flags(cpu, value, cpu->hl.byte.low);

    if (!block_repeats(cpu) || 0u == cpu->bc.byte.high)
    {
        end_instruction_here(cpu);
    }
    else
    {
        block_repeat_undocumented(cpu);
    }
}

/* the DD and FD sets: HL becomes IX or IY, and (HL) becomes (IX+d) */

/**
 * The displacement is signed and added to the index register, and the answer
 * is kept in WZ. That is not a convenience: WZ really is where the real part
 * forms the address, which is why every indexed instruction leaves MEMPTR
 * holding IX+d afterwards.
 */
static void exit_to_displacement(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->wz.word = (uint16_t)(index_pair(cpu)->word + (int8_t)cpu->bus_data);
}

static void at_wz(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->bus_addr = cpu->wz.word;
}

/** LD r,(IX+d): the register named is the plain one, never the index half. */
static void execute_ld_plain_reg_from_bus(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    uint8_t *destination = register_slot_plain(cpu, (uint8_t)(cpu->opcode >> 3));
    if (destination)
    {
        *destination = cpu->bus_data;
    }
}

static void enter_write_plain_reg_at_wz(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    const uint8_t *source = register_slot_plain(cpu, cpu->opcode);
    write_at(cpu, cpu->wz.word, source ? *source : 0u);
}

/**
 * DD CB puts the displacement before the operation byte, and that byte arrives
 * through an ordinary memory read rather than a fetch - so R is incremented
 * twice for the whole instruction, not three times.
 */
static void exit_latch_operation(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->opcode = cpu->bus_data;
}

static void exit_index_cb_apply(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    const uint8_t group = (uint8_t)(cpu->opcode >> 6);
    const uint8_t bit = (uint8_t)((cpu->opcode >> 3) & 7u);

    if (1u == group)
    {
        /* BIT has nothing to write back, so X and Y come from the address it
           formed instead of from a result */
        alu_bit(cpu, bit, cpu->bus_data, cpu->wz.byte.high);
        end_instruction_here(cpu);
        return;
    }

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

    /* Undocumented, and universal: the low three bits still name a register,
       and the result is copied there as well as written back to memory. Only
       the encoding for (HL) leaves it in memory alone. */
    uint8_t *slot = register_slot_plain(cpu, cpu->opcode);
    if (slot)
    {
        *slot = cpu->bus_data;
    }
}

/**
 * A prefix is a whole M1 cycle that decodes nothing: it costs four T-states,
 * increments R a second time, and tells the next fetch which table to use.
 */
static void execute_prefix(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    switch (cpu->opcode)
    {
    case 0xDDu:
    case 0xFDu:
        /* A second index prefix simply replaces the first, at four T-states
           each: DD FD 21 is LD IY,nn with four wasted cycles in front. */
        cpu->index = cpu->opcode;
        cpu->prefix = 0;
        break;
    case 0xEDu:
        /* ED ignores an index prefix entirely rather than combining with it */
        cpu->prefix = 0xEDu;
        cpu->index = 0;
        break;
    default:
        cpu->prefix = 0xCBu;
        break;
    }
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
static const z80_instr instr_prefix = {NO_CYCLES, 0u, execute_prefix};
static const z80_instr instr_cb_reg = {NO_CYCLES, 0u, execute_cb_reg};

/* CB with a memory operand: 15 T-states, of which the read is given four */
static const z80_instr instr_cb_mem = {
    {{&mem_read_seq, at_hl, exit_cb_bus}, {&idle1_seq, NULL, NULL}, {&mem_write_seq, at_hl, NULL}}, 3u, NULL};

/* BIT reads and tests but never writes back, so it is three T-states shorter */
static const z80_instr instr_cb_bit_mem = {
    {{&mem_read_seq, at_hl, NULL}, {&idle1_seq, NULL, NULL}}, 2u, execute_cb_bit_memory};

/*
 * The DD and FD sets. Everything that does not touch (HL) reuses the ordinary
 * descriptor and simply finds an index register where HL would have been, so
 * only the six shapes below have to exist: the ones that turn a register
 * operand into a displaced memory access, which costs a byte to read and five
 * T-states to add up.
 */
static const z80_instr instr_index_ld_reg_mem = {
    {{&mem_read_seq, at_pc, exit_to_displacement}, {&idle5_seq, NULL, NULL}, {&mem_read_seq, at_wz, NULL}},
    3u,
    execute_ld_plain_reg_from_bus};

static const z80_instr instr_index_ld_mem_reg = {{{&mem_read_seq, at_pc, exit_to_displacement},
                                                  {&idle5_seq, NULL, NULL},
                                                  {&mem_write_seq, enter_write_plain_reg_at_wz, NULL}},
                                                 3u,
                                                 NULL};

static const z80_instr instr_index_alu_mem = {
    {{&mem_read_seq, at_pc, exit_to_displacement}, {&idle5_seq, NULL, NULL}, {&mem_read_seq, at_wz, NULL}},
    3u,
    execute_alu_bus};

/* LD (IX+d),n reads both bytes first, so its wait is two T-states, not five */
static const z80_instr instr_index_ld_mem_n = {{{&mem_read_seq, at_pc, exit_to_displacement},
                                                {&mem_read_seq, at_pc, NULL},
                                                {&idle2_seq, NULL, NULL},
                                                {&mem_write_seq, at_wz, NULL}},
                                               4u,
                                               NULL};

static const z80_instr instr_index_inc_mem = {{{&mem_read_seq, at_pc, exit_to_displacement},
                                               {&idle5_seq, NULL, NULL},
                                               {&mem_read_seq, at_wz, exit_inc_bus},
                                               {&idle1_seq, NULL, NULL},
                                               {&mem_write_seq, at_wz, NULL}},
                                              5u,
                                              NULL};

static const z80_instr instr_index_dec_mem = {{{&mem_read_seq, at_pc, exit_to_displacement},
                                               {&idle5_seq, NULL, NULL},
                                               {&mem_read_seq, at_wz, exit_dec_bus},
                                               {&idle1_seq, NULL, NULL},
                                               {&mem_write_seq, at_wz, NULL}},
                                              5u,
                                              NULL};

/*
 * DD CB is the odd one out of the whole instruction set: the displacement
 * comes before the operation byte, and that byte is collected by an ordinary
 * memory read rather than a fetch. Twenty-three T-states, or twenty for BIT,
 * which stops before the write.
 */
static const z80_instr instr_index_cb = {{{&mem_read_seq, at_pc, exit_to_displacement},
                                          {&mem_read_seq, at_pc, exit_latch_operation},
                                          {&idle2_seq, NULL, NULL},
                                          {&mem_read_seq, at_wz, NULL},
                                          {&idle1_seq, NULL, exit_index_cb_apply},
                                          {&mem_write_seq, at_wz, NULL}},
                                         6u,
                                         NULL};

/* the ED set. Every one of these carries the four T-states of the prefix on
   top of what is listed here, so ED 44 (NEG) costs eight in total. */
static const z80_instr instr_ed_neg = {NO_CYCLES, 0u, execute_neg};
static const z80_instr instr_ed_im = {NO_CYCLES, 0u, execute_set_interrupt_mode};
/* an ED opcode with no meaning behaves as two NOPs, which is what it is */
static const z80_instr instr_ed_nop = {NO_CYCLES, 0u, NULL};

static const z80_instr instr_ed_adc_hl = {{{&idle7_seq, NULL, NULL}}, 1u, execute_adc_hl};
static const z80_instr instr_ed_sbc_hl = {{{&idle7_seq, NULL, NULL}}, 1u, execute_sbc_hl};

static const z80_instr instr_ed_ld_i_a = {{{&idle1_seq, NULL, NULL}}, 1u, execute_ld_i_from_a};
static const z80_instr instr_ed_ld_r_a = {{{&idle1_seq, NULL, NULL}}, 1u, execute_ld_r_from_a};
static const z80_instr instr_ed_ld_a_i = {{{&idle1_seq, NULL, NULL}}, 1u, execute_ld_a_from_i};
static const z80_instr instr_ed_ld_a_r = {{{&idle1_seq, NULL, NULL}}, 1u, execute_ld_a_from_r};

static const z80_instr instr_ed_retn = {
    {{&mem_read_seq, at_pop, exit_to_wz_low}, {&mem_read_seq, at_pop, exit_return_from_interrupt}}, 2u, NULL};

static const z80_instr instr_ed_in_c = {{{&io_read_seq, enter_io_at_bc, NULL}}, 1u, execute_in_c};
static const z80_instr instr_ed_out_c = {{{&io_write_seq, enter_out_c, NULL}}, 1u, execute_out_c};

static const z80_instr instr_ed_ld_nn_rp = {{{&mem_read_seq, at_pc, exit_to_tmp_low},
                                             {&mem_read_seq, at_pc, exit_to_tmp_high},
                                             {&mem_write_seq, enter_write_rp_low, NULL},
                                             {&mem_write_seq, enter_write_rp_high, NULL}},
                                            4u,
                                            execute_memptr_after_tmp};

static const z80_instr instr_ed_ld_rp_nn = {{{&mem_read_seq, at_pc, exit_to_tmp_low},
                                             {&mem_read_seq, at_pc, exit_to_tmp_high},
                                             {&mem_read_seq, at_tmp, exit_to_rp_low},
                                             {&mem_read_seq, at_tmp_high, NULL}},
                                            4u,
                                            execute_ld_rp_from_memory};

static const z80_instr instr_ed_rrd = {
    {{&mem_read_seq, at_hl, exit_rrd}, {&idle4_seq, NULL, NULL}, {&mem_write_seq, enter_block_write_hl, NULL}},
    3u,
    NULL};

static const z80_instr instr_ed_rld = {
    {{&mem_read_seq, at_hl, exit_rld}, {&idle4_seq, NULL, NULL}, {&mem_write_seq, enter_block_write_hl, NULL}},
    3u,
    NULL};

/* The four families of block instruction, one descriptor each. The last cycle
   only runs when the instruction repeats, which is where the extra five
   T-states of LDIR over LDI come from. */
static const z80_instr instr_ed_block_transfer = {{{&mem_read_seq, at_hl, NULL},
                                                   {&mem_write_seq, enter_block_write_de, NULL},
                                                   {&idle2_seq, NULL, exit_block_transfer},
                                                   {&idle5_seq, enter_block_repeat, NULL}},
                                                  4u,
                                                  NULL};

static const z80_instr instr_ed_block_compare = {
    {{&mem_read_seq, at_hl, NULL}, {&idle5_seq, NULL, exit_block_compare}, {&idle5_seq, enter_block_repeat, NULL}},
    3u,
    NULL};

static const z80_instr instr_ed_block_in = {{{&idle1_seq, NULL, NULL},
                                             {&io_read_seq, enter_io_at_bc, NULL},
                                             {&mem_write_seq, enter_block_write_hl, exit_block_in},
                                             {&idle5_seq, enter_block_repeat, NULL}},
                                            4u,
                                            NULL};

static const z80_instr instr_ed_block_out = {{{&idle1_seq, NULL, NULL},
                                              {&mem_read_seq, at_hl, NULL},
                                              {&io_write_seq, enter_block_out, exit_block_out},
                                              {&idle5_seq, enter_block_repeat, NULL}},
                                             4u,
                                             NULL};

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

/* --- interrupts --- */

/**
 * Where an accepted interrupt goes. Mode 1 always vectors to 0038. Mode 0
 * executes whatever the device put on the bus, which in every system that uses
 * it is an RST - so the RST is honoured and nothing else is, which is a real
 * limitation and a documented one.
 */
static void exit_interrupt_vector(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    const uint16_t target = (0u == cpu->im) ? (uint16_t)(cpu->tmp.byte.low & 0x38u) : 0x0038u;
    cpu->pc.word = target;
    cpu->wz.word = target;
}

static void exit_nmi_vector(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->pc.word = 0x0066u;
    cpu->wz.word = 0x0066u;
}

/** Mode 2 forms a table address from I and the byte the device supplied. */
static void at_interrupt_table(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->tmp.byte.high = cpu->i;
    cpu->bus_addr = cpu->tmp.word;
}

static void at_interrupt_table_high(z80_t *cpu, z80_pins_t *pins)
{
    (void)pins;
    cpu->bus_addr = (uint16_t)(cpu->tmp.word + 1u);
}

static const z80_instr instr_nmi = {{{&nmi_ack_seq, NULL, NULL},
                                     {&mem_write_seq, enter_push_pc_high, NULL},
                                     {&mem_write_seq, enter_push_pc_low, exit_nmi_vector}},
                                    3u,
                                    NULL};

/* modes 0 and 1: acknowledge, push, vector. Thirteen T-states. */
static const z80_instr instr_interrupt = {{{&int_ack_seq, NULL, exit_to_tmp_low},
                                           {&mem_write_seq, enter_push_pc_high, NULL},
                                           {&mem_write_seq, enter_push_pc_low, exit_interrupt_vector}},
                                          3u,
                                          NULL};

/* mode 2: and then two more reads to fetch the handler's address. Nineteen. */
static const z80_instr instr_interrupt_mode2 = {{{&int_ack_seq, NULL, exit_to_tmp_low},
                                                 {&mem_write_seq, enter_push_pc_high, NULL},
                                                 {&mem_write_seq, enter_push_pc_low, NULL},
                                                 {&mem_read_seq, at_interrupt_table, exit_to_wz_low},
                                                 {&mem_read_seq, at_interrupt_table_high, exit_return_to_wz_high}},
                                                5u,
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
            return &instr_prefix; /* DD, ED and FD */
        case 6:
            return &instr_alu_n;
        default:
            return &instr_rst;
        }
    }
}

/**
 * The ED table is mostly empty. Two bands of it carry everything: 40-7F holds
 * the I/O, the 16-bit arithmetic and the interrupt control, and a corner of
 * A0-BF holds the block instructions. Every other encoding is a two-byte NOP,
 * which is what the hardware does rather than a convenience.
 */
static const z80_instr *decode_ed(const z80_t *cpu)
{
    const uint8_t opcode = cpu->opcode;
    const uint8_t x = (uint8_t)(opcode >> 6);
    const uint8_t y = (uint8_t)((opcode >> 3) & 7u);
    const uint8_t z = (uint8_t)(opcode & 7u);
    const uint8_t q = (uint8_t)(y & 1u);

    if (2u == x && z <= 3u && y >= 4u)
    {
        switch (z)
        {
        case 0:
            return &instr_ed_block_transfer; /* LDI LDD LDIR LDDR */
        case 1:
            return &instr_ed_block_compare; /* CPI CPD CPIR CPDR */
        case 2:
            return &instr_ed_block_in; /* INI IND INIR INDR */
        default:
            return &instr_ed_block_out; /* OUTI OUTD OTIR OTDR */
        }
    }

    if (1u != x)
    {
        return &instr_ed_nop;
    }

    switch (z)
    {
    case 0:
        return &instr_ed_in_c;
    case 1:
        return &instr_ed_out_c;
    case 2:
        return q ? &instr_ed_adc_hl : &instr_ed_sbc_hl;
    case 3:
        return q ? &instr_ed_ld_rp_nn : &instr_ed_ld_nn_rp;
    case 4:
        return &instr_ed_neg;
    case 5:
        return &instr_ed_retn; /* RETI differs from RETN only to a peripheral */
    case 6:
        return &instr_ed_im;
    default:
        switch (y)
        {
        case 0:
            return &instr_ed_ld_i_a;
        case 1:
            return &instr_ed_ld_r_a;
        case 2:
            return &instr_ed_ld_a_i;
        case 3:
            return &instr_ed_ld_a_r;
        case 4:
            return &instr_ed_rrd;
        case 5:
            return &instr_ed_rld;
        default:
            return &instr_ed_nop;
        }
    }
}

/**
 * Under DD or FD the ordinary table still applies - the index register has
 * simply taken HL's place, which index_pair() has already arranged. Only the
 * encodings that name (HL) as memory need a different shape, because those
 * grow a displacement byte and the time to add it up.
 */
static const z80_instr *decode_indexed(const z80_t *cpu)
{
    const uint8_t opcode = cpu->opcode;
    const uint8_t x = (uint8_t)(opcode >> 6);
    const uint8_t y = (uint8_t)((opcode >> 3) & 7u);
    const uint8_t z = (uint8_t)(opcode & 7u);

    /* another prefix byte replaces this one, or hands over to ED */
    if (0xCBu == opcode)
    {
        return &instr_index_cb;
    }
    if (0xDDu == opcode || 0xEDu == opcode || 0xFDu == opcode)
    {
        return &instr_prefix;
    }

    switch (x)
    {
    case 0:
        if (6u == y)
        {
            if (4u == z)
            {
                return &instr_index_inc_mem;
            }
            if (5u == z)
            {
                return &instr_index_dec_mem;
            }
            if (6u == z)
            {
                return &instr_index_ld_mem_n;
            }
        }
        break;

    case 1:
        if (6u == y && 6u == z)
        {
            break; /* HALT is HALT, prefix or not */
        }
        if (6u == z)
        {
            return &instr_index_ld_reg_mem;
        }
        if (6u == y)
        {
            return &instr_index_ld_mem_reg;
        }
        break;

    case 2:
        if (6u == z)
        {
            return &instr_index_alu_mem;
        }
        break;

    default:
        break;
    }

    /* Everything else - including EX DE,HL, which the prefix pointedly does
       not touch - decodes exactly as it would unprefixed. */
    return decode_base(cpu);
}

/** Which table the byte just fetched should be read against. */
static const z80_instr *decode(const z80_t *cpu)
{
    if (0xEDu == cpu->prefix)
    {
        return decode_ed(cpu);
    }
    if (0xCBu == cpu->prefix)
    {
        return decode_cb(cpu);
    }
    if (0u != cpu->index)
    {
        return decode_indexed(cpu);
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
/**
 * Leaving HALT. PC sits *on* the HALT while halted, so the return address the
 * acknowledge is about to push has to be the instruction after it. Getting
 * this wrong is invisible until something returns.
 */
static void wake_from_halt(z80_t *cpu, z80_pins_t *pins)
{
    if (cpu->halted)
    {
        cpu->halted = false;
        cpu->pc.word = (uint16_t)(cpu->pc.word + 1u);
        pins->ctrl &= ~(uint32_t)Z80_HALT;
    }
}

/**
 * @brief Decide whether an interrupt starts here, at an instruction boundary.
 *
 * NMI wins over INT and ignores IFF1 - that is what makes it non-maskable. It
 * copies IFF1 into IFF2 on the way so RETN can put it back.
 *
 * Nothing is accepted while @c int_inhibit is set, which is how EI's one
 * instruction of grace works: EI enables interrupts and sets the inhibit, so
 * the instruction after it runs first. Without that, EI / RET could be
 * interrupted between the two and the return address would be lost - which is
 * the entire reason the delay exists on the real part.
 *
 * @return The sequence to run instead of a fetch, or NULL to fetch normally.
 */
static const z80_instr *accept_interrupt(z80_t *cpu, z80_pins_t *pins)
{
    if (cpu->int_inhibit)
    {
        return NULL;
    }

    if (cpu->nmi_latched)
    {
        cpu->nmi_latched = false;
        cpu->iff2 = cpu->iff1;
        cpu->iff1 = false;
        wake_from_halt(cpu, pins);
        return &instr_nmi;
    }

    if (cpu->iff1 && (pins->ctrl & Z80_INT))
    {
        cpu->iff1 = false;
        cpu->iff2 = false;
        wake_from_halt(cpu, pins);
        return (2u == cpu->im) ? &instr_interrupt_mode2 : &instr_interrupt;
    }

    return NULL;
}

/** Begin an interrupt sequence in place of the fetch that would have run. */
static void begin_interrupt(z80_t *cpu, z80_pins_t *pins, const z80_instr *sequence)
{
    cpu->instr = sequence;
    cpu->cycle = 0;
    cpu->cycle_limit = sequence->cycle_count;
    cpu->step = 0;
    cpu->seq = sequence->cycles[0].seq;
    if (sequence->cycles[0].enter)
    {
        sequence->cycles[0].enter(cpu, pins);
    }
}

/**
 * RESET has to be held for three clocks to take, which is why a glitch on the
 * pin does not restart the machine. The architectural state goes back to what
 * the part powers up with; the clock and the edge counter are the host's and
 * are left alone.
 */
static void apply_reset(z80_t *cpu, z80_pins_t *pins)
{
    cpu->pc.word = 0;
    cpu->i = 0;
    cpu->r = 0;
    cpu->im = 0;
    cpu->iff1 = false;
    cpu->iff2 = false;
    cpu->halted = false;
    cpu->nmi_latched = false;
    cpu->int_inhibit = false;
    cpu->prefix = 0;
    cpu->index = 0;
    cpu->instr = NULL;
    cpu->cycle = 0;
    cpu->cycle_limit = 0;
    cpu->step = 0;
    cpu->seq = &m1_fetch_seq;

    pins->ctrl &= ~(uint32_t)Z80_OUTPUT_PINS;
}

static uint32_t advance(z80_t *cpu, z80_pins_t *pins)
{
    const uint16_t address_before = pins->A;
    const uint8_t data_before = pins->D;
    const uint32_t ctrl_before = pins->ctrl;

    /*
     * While the host holds the bus the CPU does nothing at all: the cursor
     * stays where it is, and picks up on the same step when BUSRQ is released.
     * Refresh stops with everything else, which on a machine with DRAM is the
     * host's problem to bound rather than a detail.
     */
    /*
     * The asynchronous inputs, behind a single test. Every edge pays for this
     * check, and almost every edge has none of them asserted, so the work goes
     * inside the branch rather than in front of it - doing it the other way
     * cost the core a third of its throughput.
     */
    if (pins->ctrl & (Z80_NMI | Z80_RESET))
    {
        /* NMI is edge triggered, not level: latch the transition, or a pin
           held asserted for a millisecond becomes thousands of interrupts. */
        if (!cpu->nmi_previous && (pins->ctrl & Z80_NMI))
        {
            cpu->nmi_latched = true;
        }
        cpu->nmi_previous = 0u != (pins->ctrl & Z80_NMI);

        if (pins->ctrl & Z80_RESET)
        {
            ++cpu->edges;
            if (cpu->reset_held < 255u)
            {
                ++cpu->reset_held;
            }
            if (cpu->reset_held >= 6u) /* three T-states */
            {
                apply_reset(cpu, pins);
            }
            return (pins->ctrl ^ ctrl_before) & Z80_OUTPUT_PINS;
        }
        cpu->reset_held = 0;
    }
    else
    {
        cpu->nmi_previous = false;
        cpu->reset_held = 0;
    }

    if (cpu->bus_released)
    {
        ++cpu->edges;
        if (0u == (pins->ctrl & Z80_BUSRQ))
        {
            cpu->bus_released = false;
            pins->ctrl &= ~(uint32_t)Z80_BUSAK;
        }
        return (pins->ctrl ^ ctrl_before) & Z80_OUTPUT_PINS;
    }

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

    /*
     * BUSRQ is sampled on the rising edge of a machine cycle's last T-state,
     * so a request is granted between machine cycles rather than between
     * instructions - a host can take the bus in the middle of an LDIR.
     */
    if ((pins->ctrl & Z80_BUSRQ) && cpu->seq->count >= 2u && executed == (uint8_t)(cpu->seq->count - 2u))
    {
        cpu->bus_request_latched = true;
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
            /* A prefix has just set these; anything else has finished with
               them, and the next fetch starts from the ordinary table. */
            const bool was_prefix = (cpu->instr == &instr_prefix);
            if (!was_prefix)
            {
                cpu->prefix = 0;
                cpu->index = 0;
            }
            cpu->instr = NULL;
            cpu->seq = &m1_fetch_seq;

            /*
             * This is the instruction boundary, and the only place an
             * interrupt can be accepted. A prefix is not one: DD and its
             * opcode are a single instruction, and an interrupt taken between
             * them would resume at a byte that means something else.
             */
            if (!was_prefix)
            {
                const z80_instr *interrupt = accept_interrupt(cpu, pins);
                cpu->int_inhibit = false;
                if (interrupt)
                {
                    begin_interrupt(cpu, pins, interrupt);
                }
            }
        }
    }

    /*
     * Grant the bus now the cycle has finished. Everything the CPU drives is
     * let go together; the host is told by BUSAK, which is the only thing this
     * interface can say - a pin struct owned by the caller has no way to
     * express high impedance, so BUSAK asserted means "these are not mine".
     */
    if (cpu->bus_request_latched && 0u == cpu->step)
    {
        cpu->bus_request_latched = false;
        cpu->bus_released = true;
        pins->ctrl &= ~(uint32_t)(Z80_M1 | Z80_MREQ | Z80_IORQ | Z80_RD | Z80_WR | Z80_RFSH);
        pins->ctrl |= Z80_BUSAK;
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

void z80_set_state(z80_t *cpu, const z80_state_t *state)
{
    if (!cpu || !state)
    {
        return;
    }

    cpu->af.word = state->af;
    cpu->bc.word = state->bc;
    cpu->de.word = state->de;
    cpu->hl.word = state->hl;
    cpu->af_alt.word = state->af_alt;
    cpu->bc_alt.word = state->bc_alt;
    cpu->de_alt.word = state->de_alt;
    cpu->hl_alt.word = state->hl_alt;
    cpu->ix.word = state->ix;
    cpu->iy.word = state->iy;
    cpu->sp.word = state->sp;
    cpu->pc.word = state->pc;
    cpu->wz.word = state->wz;
    cpu->i = state->i;
    cpu->r = state->r;
    cpu->im = state->im;
    cpu->iff1 = state->iff1;
    cpu->iff2 = state->iff2;
    cpu->halted = state->halted;
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
    uint8_t index;
    uint8_t bus_released;
    uint8_t nmi_latched;
    uint8_t int_inhibit;
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
    snapshot.index = cpu->index;
    snapshot.bus_released = cpu->bus_released ? 1u : 0u;
    snapshot.nmi_latched = cpu->nmi_latched ? 1u : 0u;
    snapshot.int_inhibit = cpu->int_inhibit ? 1u : 0u;
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
    cpu->index = snapshot.index;
    cpu->bus_released = 0 != snapshot.bus_released;
    cpu->nmi_latched = 0 != snapshot.nmi_latched;
    cpu->int_inhibit = 0 != snapshot.int_inhibit;
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
