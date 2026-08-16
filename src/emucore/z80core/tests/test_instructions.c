/**
 * @file   test_instructions.c
 * @brief  The unprefixed instruction set: results, flags and T-state counts
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
 *
 * This file is part of Z80core, released under the terms of the GNU General
 * Public License version 2. See LICENSE.md for the full text.
 *
 * test_core.c covers the engine - the clock, the pins, the fetch. This file
 * covers what the instructions do, and how long they take. The timing checks
 * matter as much as the results: an emulator that computes the right answer in
 * the wrong number of T-states is wrong for anything driving real hardware,
 * and the whole point of this core is that it drives real hardware.
 *
 * Flag expectations include the undocumented X and Y bits, which copy bits 3
 * and 5 of the result. They cost nothing to get right and are checked by FUSE,
 * so there is no reason to leave them for later.
 */

#include "z80core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition, ...)                                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            ++failures;                                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
        }                                                                                                              \
    } while (0)

/* ---------------------------------------------------------------- */
/* A machine: memory, I/O ports, and a clock                         */
/* ---------------------------------------------------------------- */

/**
 * What a host has to provide. This is deliberately the whole of it - the core
 * asks for bytes through the pins and never reaches for anything itself, so a
 * test harness and a Proteus device script do the same job.
 */
typedef struct
{
    uint8_t memory[0x10000];
    uint8_t io[0x10000];
    int level;             /**< the clock line the host drives */
    uint16_t last_io_port; /**< where the last I/O cycle went */
    unsigned io_cycles;
} machine;

/* 128 KB of it, so not on the stack */
static machine world;

static z80_t *boot(const uint8_t *program, size_t size)
{
    memset(&world, 0, sizeof world);
    memcpy(world.memory, program, size);
    return z80_new();
}

static void clock_edge(z80_t *cpu, z80_pins_t *pins)
{
    const bool io_before = 0 != (pins->ctrl & Z80_IORQ);

    world.level ^= 1;
    (void)z80_tick(cpu, pins, world.level);

    if (pins->ctrl & Z80_MREQ)
    {
        if (pins->ctrl & Z80_RD)
        {
            pins->D = world.memory[pins->A];
        }
        else if (pins->ctrl & Z80_WR)
        {
            world.memory[pins->A] = pins->D;
        }
    }

    if (pins->ctrl & Z80_IORQ)
    {
        if (!io_before)
        {
            ++world.io_cycles;
            world.last_io_port = pins->A;
        }
        if (pins->ctrl & Z80_RD)
        {
            pins->D = world.io[pins->A];
        }
        else if (pins->ctrl & Z80_WR)
        {
            world.io[pins->A] = pins->D;
        }
    }
}

/**
 * Run exactly one instruction and return what it cost in T-states.
 *
 * The boundary is the rising edge of M1, which is where a logic analyser would
 * put it too. That edge cannot be seen without clocking it, so it is consumed
 * here and counted against the instruction it begins - which is what makes
 * consecutive calls line up instead of drifting one edge further each time.
 * Returns -1 if no next fetch ever starts.
 */
static int run_one(z80_t *cpu, z80_pins_t *pins)
{
    int edges = 1; /* the T1 rising edge of this instruction's fetch */

    if (0 == (pins->ctrl & Z80_M1))
    {
        clock_edge(cpu, pins); /* the first call after reset: start the fetch */
    }

    for (int i = 0; i < 400; ++i)
    {
        const bool was_m1 = 0 != (pins->ctrl & Z80_M1);
        clock_edge(cpu, pins);
        ++edges;

        if (!was_m1 && (pins->ctrl & Z80_M1))
        {
            return (edges - 1) / 2;
        }
    }
    return -1;
}

/**
 * A prefixed instruction is two M1 cycles, so it crosses the boundary run_one
 * measures between. Its cost is the pair of them added together, which is what
 * the manual quotes.
 */
static int run_prefixed(z80_t *cpu, z80_pins_t *pins)
{
    const int prefix_tstates = run_one(cpu, pins);
    const int body_tstates = run_one(cpu, pins);
    return prefix_tstates + body_tstates;
}

static void run_many(z80_t *cpu, z80_pins_t *pins, int instructions)
{
    for (int i = 0; i < instructions; ++i)
    {
        (void)run_one(cpu, pins);
    }
}

static uint8_t reg_a(const z80_t *cpu)
{
    return (uint8_t)(z80_get(cpu, Z80_REG_AF) >> 8);
}

static uint8_t reg_f(const z80_t *cpu)
{
    return (uint8_t)z80_get(cpu, Z80_REG_AF);
}

/* ---------------------------------------------------------------- */
/* Flags                                                             */
/* ---------------------------------------------------------------- */

/**
 * One ALU case: put @c a in A and @c operand in B, run the opcode, and compare
 * the whole of AF. Comparing all eight bits rather than the interesting ones is
 * the point - a flag set that should have stayed clear is still a bug.
 */
typedef struct
{
    const char *name;
    uint8_t opcode;
    uint8_t a;
    uint8_t operand;
    uint8_t flags_in;
    uint8_t expect_a;
    uint8_t expect_f;
} alu_case;

static void test_alu_flags(void)
{
    static const alu_case cases[] = {
        /*                              A     B     F in  A out F out */
        /* 55 is 0101 0101, so bits 3 and 5 are both clear and F is empty */
        {"ADD A,B", 0x80, 0x44, 0x11, 0x00, 0x55, 0x00},
        {"ADD A,B undocumented bits", 0x80, 0x20, 0x08, 0x00, 0x28, 0x28},
        {"ADD A,B overflow", 0x80, 0x80, 0x80, 0x00, 0x00, 0x45},
        {"ADD A,B half carry", 0x80, 0x0F, 0x01, 0x00, 0x10, 0x10},
        {"ADC A,B with carry", 0x88, 0x0F, 0x00, 0x01, 0x10, 0x10},
        {"SUB B borrow", 0x90, 0x00, 0x01, 0x00, 0xFF, 0xBB},
        {"SBC A,B with carry", 0x98, 0x00, 0x00, 0x01, 0xFF, 0xBB},
        {"AND B", 0xA0, 0xFF, 0x0F, 0x00, 0x0F, 0x1C},
        {"XOR B to zero", 0xA8, 0x3C, 0x3C, 0x00, 0x00, 0x44},
        {"OR B", 0xB0, 0x00, 0x00, 0x00, 0x00, 0x44},
        /* CP takes X and Y from the operand, not the result - nothing else
           in the instruction set does this, so it gets its own case */
        {"CP B equal", 0xB8, 0x20, 0x20, 0x00, 0x20, 0x62},
        {"CP B undocumented bits", 0xB8, 0x00, 0x28, 0x00, 0x00, 0xBB},
    };

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i)
    {
        const alu_case *test = &cases[i];
        const uint8_t program[] = {test->opcode, 0x00};
        z80_t *cpu = boot(program, sizeof program);
        z80_pins_t pins = {0};

        z80_set(cpu, Z80_REG_AF, (uint16_t)(((uint16_t)test->a << 8) | test->flags_in));
        z80_set(cpu, Z80_REG_BC, (uint16_t)((uint16_t)test->operand << 8));

        const int tstates = run_one(cpu, &pins);

        CHECK(4 == tstates, "%s should take 4 T-states, took %d", test->name, tstates);
        CHECK(test->expect_a == reg_a(cpu), "%s: A should be %02X, is %02X", test->name, test->expect_a, reg_a(cpu));
        CHECK(test->expect_f == reg_f(cpu), "%s: F should be %02X, is %02X", test->name, test->expect_f, reg_f(cpu));

        z80_free(cpu);
    }
}

/** INC and DEC leave carry alone; everything else about them moves. */
static void test_inc_dec_flags(void)
{
    static const struct
    {
        const char *name;
        uint8_t opcode;
        uint8_t before;
        uint8_t flags_in;
        uint8_t expect;
        uint8_t expect_f;
    } cases[] = {
        {"INC B to 80", 0x04, 0x7F, 0x00, 0x80, 0x94},      {"INC B keeps carry", 0x04, 0x7F, 0x01, 0x80, 0x95},
        {"INC B wraps", 0x04, 0xFF, 0x00, 0x00, 0x50},      {"DEC B to zero", 0x05, 0x01, 0x00, 0x00, 0x42},
        {"DEC B underflows", 0x05, 0x00, 0x00, 0xFF, 0xBA}, {"DEC B to 7F", 0x05, 0x80, 0x00, 0x7F, 0x3E},
    };

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i)
    {
        const uint8_t program[] = {cases[i].opcode, 0x00};
        z80_t *cpu = boot(program, sizeof program);
        z80_pins_t pins = {0};

        z80_set(cpu, Z80_REG_AF, cases[i].flags_in);
        z80_set(cpu, Z80_REG_BC, (uint16_t)((uint16_t)cases[i].before << 8));
        (void)run_one(cpu, &pins);

        const uint8_t got = (uint8_t)(z80_get(cpu, Z80_REG_BC) >> 8);
        CHECK(cases[i].expect == got, "%s: B should be %02X, is %02X", cases[i].name, cases[i].expect, got);
        CHECK(cases[i].expect_f == reg_f(cpu), "%s: F should be %02X, is %02X", cases[i].name, cases[i].expect_f,
              reg_f(cpu));

        z80_free(cpu);
    }
}

/**
 * DAA is the one instruction that reads N and H rather than writing them, and
 * the only reason those flags exist at all. 99 + 1 = 00 with carry is the
 * case that proves it.
 */
static void test_daa(void)
{
    static const uint8_t program[] = {0xC6, 0x01, 0x27}; /* ADD A,1 ; DAA */

    z80_t *cpu = boot(program, sizeof program);
    z80_pins_t pins = {0};
    z80_set(cpu, Z80_REG_AF, 0x0900);
    run_many(cpu, &pins, 2);
    CHECK(0x10 == reg_a(cpu), "DAA after 09+01 should give 10, gives %02X", reg_a(cpu));

    z80_free(cpu);

    cpu = boot(program, sizeof program);
    memset(&pins, 0, sizeof pins);
    z80_set(cpu, Z80_REG_AF, 0x9900);
    run_many(cpu, &pins, 2);
    CHECK(0x00 == reg_a(cpu), "DAA after 99+01 should give 00, gives %02X", reg_a(cpu));
    CHECK(0 != (reg_f(cpu) & 0x01u), "DAA after 99+01 should carry");
    CHECK(0 != (reg_f(cpu) & 0x40u), "DAA after 99+01 should set Z");

    z80_free(cpu);
}

/** ADD HL,rr carries out of bit 11 into H and bit 15 into C, and only those. */
static void test_add_hl(void)
{
    static const uint8_t program[] = {0x09, 0x00}; /* ADD HL,BC */

    z80_t *cpu = boot(program, sizeof program);
    z80_pins_t pins = {0};

    z80_set(cpu, Z80_REG_HL, 0x0FFF);
    z80_set(cpu, Z80_REG_BC, 0x0001);
    z80_set(cpu, Z80_REG_AF, 0x00C0); /* S and Z set, and must survive */

    const int tstates = run_one(cpu, &pins);

    CHECK(11 == tstates, "ADD HL,BC should take 11 T-states, took %d", tstates);
    CHECK(0x1000 == z80_get(cpu, Z80_REG_HL), "HL should be 1000, is %04X", z80_get(cpu, Z80_REG_HL));
    CHECK(0 != (reg_f(cpu) & 0x10u), "carry out of bit 11 should set H");
    CHECK(0 == (reg_f(cpu) & 0x01u), "there was no carry out of bit 15");
    CHECK(0xC0 == (reg_f(cpu) & 0xC0u), "ADD HL,rr must not touch S or Z");
    CHECK(0x1000 == z80_get(cpu, Z80_REG_WZ), "WZ should be HL+1 = 1000, is %04X", z80_get(cpu, Z80_REG_WZ));

    z80_free(cpu);
}

/* ---------------------------------------------------------------- */
/* Memory and the stack                                              */
/* ---------------------------------------------------------------- */

static void test_memory_loads(void)
{
    /* LD HL,4000 ; LD (HL),7B ; LD A,(HL) ; LD (2000),A ; LD BC,(...)  */
    static const uint8_t program[] = {
        0x21, 0x00, 0x40, /* LD HL,4000   */
        0x36, 0x7B,       /* LD (HL),7B   */
        0x7E,             /* LD A,(HL)    */
        0x32, 0x00, 0x20, /* LD (2000),A  */
        0x22, 0x10, 0x20, /* LD (2010),HL */
        0x2A, 0x10, 0x20  /* LD HL,(2010) */
    };

    z80_t *cpu = boot(program, sizeof program);
    z80_pins_t pins = {0};
    run_many(cpu, &pins, 6);

    CHECK(0x7B == world.memory[0x4000], "LD (HL),n stored %02X", world.memory[0x4000]);
    CHECK(0x7B == reg_a(cpu), "LD A,(HL) loaded %02X", reg_a(cpu));
    CHECK(0x7B == world.memory[0x2000], "LD (nn),A stored %02X", world.memory[0x2000]);
    CHECK(0x00 == world.memory[0x2010] && 0x40 == world.memory[0x2011], "LD (nn),HL stored %02X%02X",
          world.memory[0x2011], world.memory[0x2010]);
    CHECK(0x4000 == z80_get(cpu, Z80_REG_HL), "LD HL,(nn) read back %04X", z80_get(cpu, Z80_REG_HL));

    z80_free(cpu);
}

/** A push must land below the stack pointer, and a pop must undo it exactly. */
static void test_push_pop(void)
{
    static const uint8_t program[] = {
        0x31, 0x00, 0x80, /* LD SP,8000 */
        0x01, 0x34, 0x12, /* LD BC,1234 */
        0xC5,             /* PUSH BC    */
        0xD1              /* POP DE     */
    };

    z80_t *cpu = boot(program, sizeof program);
    z80_pins_t pins = {0};

    run_many(cpu, &pins, 2);
    const int push_t = run_one(cpu, &pins);
    CHECK(11 == push_t, "PUSH BC should take 11 T-states, took %d", push_t);
    CHECK(0x7FFE == z80_get(cpu, Z80_REG_SP), "SP should be 7FFE after a push, is %04X", z80_get(cpu, Z80_REG_SP));
    CHECK(0x34 == world.memory[0x7FFE], "the low byte should be at the lower address, found %02X",
          world.memory[0x7FFE]);
    CHECK(0x12 == world.memory[0x7FFF], "the high byte should be above it, found %02X", world.memory[0x7FFF]);

    const int pop_t = run_one(cpu, &pins);
    CHECK(10 == pop_t, "POP DE should take 10 T-states, took %d", pop_t);
    CHECK(0x1234 == z80_get(cpu, Z80_REG_DE), "POP DE recovered %04X", z80_get(cpu, Z80_REG_DE));
    CHECK(0x8000 == z80_get(cpu, Z80_REG_SP), "SP should be back at 8000, is %04X", z80_get(cpu, Z80_REG_SP));

    z80_free(cpu);
}

/** POP AF is the only way to write the undocumented flag bits directly. */
static void test_pop_af_restores_every_bit(void)
{
    static const uint8_t program[] = {
        0x31, 0x00, 0x80, /* LD SP,8000 */
        0xF1              /* POP AF     */
    };

    z80_t *cpu = boot(program, sizeof program);
    z80_pins_t pins = {0};
    world.memory[0x8000] = 0xFF; /* F */
    world.memory[0x8001] = 0x5A; /* A */

    run_many(cpu, &pins, 2);

    CHECK(0x5AFF == z80_get(cpu, Z80_REG_AF), "POP AF should give 5AFF, gave %04X", z80_get(cpu, Z80_REG_AF));

    z80_free(cpu);
}

static void test_ex_sp_hl(void)
{
    static const uint8_t program[] = {
        0x31, 0x00, 0x80, /* LD SP,8000 */
        0x21, 0x34, 0x12, /* LD HL,1234 */
        0xE3              /* EX (SP),HL */
    };

    z80_t *cpu = boot(program, sizeof program);
    z80_pins_t pins = {0};
    world.memory[0x8000] = 0xCD;
    world.memory[0x8001] = 0xAB;

    run_many(cpu, &pins, 2);
    const int tstates = run_one(cpu, &pins);

    CHECK(19 == tstates, "EX (SP),HL should take 19 T-states, took %d", tstates);
    CHECK(0xABCD == z80_get(cpu, Z80_REG_HL), "HL should have taken ABCD, is %04X", z80_get(cpu, Z80_REG_HL));
    CHECK(0x34 == world.memory[0x8000] && 0x12 == world.memory[0x8001],
          "the stack should now hold 1234, holds %02X%02X", world.memory[0x8001], world.memory[0x8000]);
    CHECK(0x8000 == z80_get(cpu, Z80_REG_SP), "EX (SP),HL must not move SP, it is %04X", z80_get(cpu, Z80_REG_SP));

    z80_free(cpu);
}

static void test_exchanges(void)
{
    static const uint8_t program[] = {0x08, 0xD9, 0xEB}; /* EX AF,AF' ; EXX ; EX DE,HL */

    z80_t *cpu = boot(program, sizeof program);
    z80_pins_t pins = {0};

    z80_set(cpu, Z80_REG_AF, 0x1111);
    z80_set(cpu, Z80_REG_AF_ALT, 0x2222);
    z80_set(cpu, Z80_REG_BC, 0x3333);
    z80_set(cpu, Z80_REG_BC_ALT, 0x4444);
    z80_set(cpu, Z80_REG_DE, 0x5555);
    z80_set(cpu, Z80_REG_HL, 0x6666);

    (void)run_one(cpu, &pins);
    CHECK(0x2222 == z80_get(cpu, Z80_REG_AF) && 0x1111 == z80_get(cpu, Z80_REG_AF_ALT), "EX AF,AF' did not swap");

    (void)run_one(cpu, &pins);
    CHECK(0x4444 == z80_get(cpu, Z80_REG_BC) && 0x3333 == z80_get(cpu, Z80_REG_BC_ALT), "EXX did not swap BC");
    CHECK(0x2222 == z80_get(cpu, Z80_REG_AF), "EXX must leave AF alone");

    const uint16_t de = z80_get(cpu, Z80_REG_DE);
    const uint16_t hl = z80_get(cpu, Z80_REG_HL);
    (void)run_one(cpu, &pins);
    CHECK(hl == z80_get(cpu, Z80_REG_DE) && de == z80_get(cpu, Z80_REG_HL), "EX DE,HL did not swap");

    z80_free(cpu);
}

/* ---------------------------------------------------------------- */
/* Control flow, where the T-state count depends on the flags        */
/* ---------------------------------------------------------------- */

static void test_call_and_return(void)
{
    static const uint8_t program[] = {
        0x31, 0x00, 0x80, /* 0000: LD SP,8000 */
        0xCD, 0x00, 0x01, /* 0003: CALL 0100  */
        0x76              /* 0006: HALT       */
    };

    z80_t *cpu = boot(program, sizeof program);
    z80_pins_t pins = {0};
    world.memory[0x0100] = 0xC9; /* RET */

    (void)run_one(cpu, &pins);

    const int call_t = run_one(cpu, &pins);
    CHECK(17 == call_t, "CALL nn should take 17 T-states, took %d", call_t);
    CHECK(0x0100 == z80_get(cpu, Z80_REG_PC), "PC should be at the routine, is %04X", z80_get(cpu, Z80_REG_PC));
    CHECK(0x7FFE == z80_get(cpu, Z80_REG_SP), "CALL should have pushed two bytes, SP is %04X",
          z80_get(cpu, Z80_REG_SP));
    CHECK(0x06 == world.memory[0x7FFE] && 0x00 == world.memory[0x7FFF],
          "the return address should be 0006, is %02X%02X", world.memory[0x7FFF], world.memory[0x7FFE]);

    const int ret_t = run_one(cpu, &pins);
    CHECK(10 == ret_t, "RET should take 10 T-states, took %d", ret_t);
    CHECK(0x0006 == z80_get(cpu, Z80_REG_PC), "RET should land after the call, is at %04X", z80_get(cpu, Z80_REG_PC));
    CHECK(0x8000 == z80_get(cpu, Z80_REG_SP), "RET should have unwound the stack, SP is %04X",
          z80_get(cpu, Z80_REG_SP));

    z80_free(cpu);
}

static void test_rst(void)
{
    static const uint8_t program[] = {
        0x31, 0x00, 0x80, /* LD SP,8000 */
        0xDF              /* RST 18     */
    };

    z80_t *cpu = boot(program, sizeof program);
    z80_pins_t pins = {0};

    (void)run_one(cpu, &pins);
    const int tstates = run_one(cpu, &pins);

    CHECK(11 == tstates, "RST should take 11 T-states, took %d", tstates);
    CHECK(0x0018 == z80_get(cpu, Z80_REG_PC), "RST 18 should jump to 0018, went to %04X", z80_get(cpu, Z80_REG_PC));
    CHECK(0x04 == world.memory[0x7FFE], "RST should push the address after it, pushed %02X", world.memory[0x7FFE]);

    z80_free(cpu);
}

/**
 * A branch costs different T-states depending on whether it is taken. Getting
 * the result right and the count wrong is the classic emulator bug, so both
 * halves are checked here.
 */
static void test_conditional_timing(void)
{
    static const struct
    {
        const char *name;
        uint8_t program[3];
        size_t length;
        uint8_t flags;
        int expect_t;
        uint16_t expect_pc;
    } cases[] = {
        /* JR NZ,+2 - Z clear means taken */
        {"JR NZ taken", {0x20, 0x02}, 2u, 0x00, 12, 0x0004},
        {"JR NZ not taken", {0x20, 0x02}, 2u, 0x40, 7, 0x0002},
        {"JR Z taken", {0x28, 0x02}, 2u, 0x40, 12, 0x0004},
        {"JR Z not taken", {0x28, 0x02}, 2u, 0x00, 7, 0x0002},
        /* JP takes the same time either way: it always reads both bytes */
        {"JP NZ taken", {0xC2, 0x34, 0x12}, 3u, 0x00, 10, 0x1234},
        {"JP NZ not taken", {0xC2, 0x34, 0x12}, 3u, 0x40, 10, 0x0003},
        {"CALL NZ taken", {0xC4, 0x34, 0x12}, 3u, 0x00, 17, 0x1234},
        {"CALL NZ not taken", {0xC4, 0x34, 0x12}, 3u, 0x40, 10, 0x0003},
        {"RET NZ taken", {0xC0}, 1u, 0x00, 11, 0xBEEF},
        {"RET NZ not taken", {0xC0}, 1u, 0x40, 5, 0x0001},
    };

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i)
    {
        z80_t *cpu = boot(cases[i].program, cases[i].length);
        z80_pins_t pins = {0};

        world.memory[0x8000] = 0xEF; /* a return address for the RET cases */
        world.memory[0x8001] = 0xBE;
        z80_set(cpu, Z80_REG_SP, 0x8000);
        z80_set(cpu, Z80_REG_AF, cases[i].flags);

        const int tstates = run_one(cpu, &pins);

        CHECK(cases[i].expect_t == tstates, "%s should take %d T-states, took %d", cases[i].name, cases[i].expect_t,
              tstates);
        CHECK(cases[i].expect_pc == z80_get(cpu, Z80_REG_PC), "%s should end at %04X, ended at %04X", cases[i].name,
              cases[i].expect_pc, z80_get(cpu, Z80_REG_PC));

        z80_free(cpu);
    }
}

/** DJNZ is the loop primitive: B counts down, and the last pass is shorter. */
static void test_djnz_loop(void)
{
    static const uint8_t program[] = {
        0x06, 0x05, /* 0000: LD B,5    */
        0x3C,       /* 0002: INC A     */
        0x10, 0xFD  /* 0003: DJNZ 0002 */
    };

    z80_t *cpu = boot(program, sizeof program);
    z80_pins_t pins = {0};

    z80_set(cpu, Z80_REG_AF, 0x0000);
    (void)run_one(cpu, &pins); /* LD B,5 */

    int taken_t = 0;
    int final_t = 0;
    for (int pass = 0; pass < 5; ++pass)
    {
        (void)run_one(cpu, &pins); /* INC A */
        const int tstates = run_one(cpu, &pins);
        if (pass < 4)
        {
            taken_t = tstates;
        }
        else
        {
            final_t = tstates;
        }
    }

    CHECK(13 == taken_t, "a taken DJNZ should take 13 T-states, took %d", taken_t);
    CHECK(8 == final_t, "the last DJNZ should take 8 T-states, took %d", final_t);
    CHECK(5 == reg_a(cpu), "the loop should have run five times, A is %02X", reg_a(cpu));
    CHECK(0 == (z80_get(cpu, Z80_REG_BC) >> 8), "B should have reached zero, is %02X",
          (unsigned)(z80_get(cpu, Z80_REG_BC) >> 8));
    CHECK(0x0005 == z80_get(cpu, Z80_REG_PC), "the loop should have fallen through to 0005, PC is %04X",
          z80_get(cpu, Z80_REG_PC));

    z80_free(cpu);
}

/* ---------------------------------------------------------------- */
/* I/O                                                               */
/* ---------------------------------------------------------------- */

/**
 * An I/O cycle is four T-states, not three: the CPU inserts one wait state by
 * itself. And A appears on the top half of the address bus, which is the part
 * everyone forgets - port FE addressed from A=7F is really port 7FFE.
 */
static void test_io_ports(void)
{
    static const uint8_t program[] = {
        0xD3, 0xFE, /* OUT (FE),A */
        0xDB, 0xFE  /* IN A,(FE)  */
    };

    z80_t *cpu = boot(program, sizeof program);
    z80_pins_t pins = {0};

    z80_set(cpu, Z80_REG_AF, 0x7F00);
    world.io[0x7FFE] = 0x00;

    const int out_t = run_one(cpu, &pins);
    CHECK(11 == out_t, "OUT (n),A should take 11 T-states, took %d", out_t);
    CHECK(0x7FFE == world.last_io_port, "the port should be 7FFE, was %04X", world.last_io_port);
    CHECK(0x7F == world.io[0x7FFE], "OUT should have written 7F, wrote %02X", world.io[0x7FFE]);

    world.io[0x7FFE] = 0xA5;
    const int in_t = run_one(cpu, &pins);
    CHECK(11 == in_t, "IN A,(n) should take 11 T-states, took %d", in_t);
    CHECK(0xA5 == reg_a(cpu), "IN should have read A5, read %02X", reg_a(cpu));
    CHECK(2 == world.io_cycles, "there should have been exactly two I/O cycles, there were %u", world.io_cycles);

    z80_free(cpu);
}

/** An I/O cycle asserts IORQ, and a memory cycle never does. */
static void test_iorq_is_distinct_from_mreq(void)
{
    static const uint8_t program[] = {0xD3, 0xFE, 0x7E}; /* OUT (FE),A ; LD A,(HL) */

    z80_t *cpu = boot(program, sizeof program);
    z80_pins_t pins = {0};
    bool saw_both_at_once = false;

    for (int i = 0; i < 60; ++i)
    {
        clock_edge(cpu, &pins);
        if ((pins.ctrl & Z80_IORQ) && (pins.ctrl & Z80_MREQ))
        {
            saw_both_at_once = true;
        }
    }

    CHECK(!saw_both_at_once, "IORQ and MREQ were asserted together");
    CHECK(world.io_cycles > 0, "no I/O cycle happened at all");

    z80_free(cpu);
}

/* ---------------------------------------------------------------- */
/* Timing, as a table                                                */
/* ---------------------------------------------------------------- */

/**
 * The T-state count of every instruction shape, against the manual. This is
 * the check that would catch a machine cycle being added in the wrong place -
 * the results can all be right while the timing is wrong.
 */
static void test_instruction_timing(void)
{
    static const struct
    {
        const char *name;
        uint8_t program[3];
        size_t length;
        int expect;
    } cases[] = {
        {"NOP", {0x00}, 1u, 4},
        {"LD B,C", {0x41}, 1u, 4},
        {"LD A,n", {0x3E, 0x55}, 2u, 7},
        {"LD B,(HL)", {0x46}, 1u, 7},
        {"LD (HL),B", {0x70}, 1u, 7},
        {"LD (HL),n", {0x36, 0x55}, 2u, 10},
        {"LD A,(BC)", {0x0A}, 1u, 7},
        {"LD (BC),A", {0x02}, 1u, 7},
        {"LD A,(nn)", {0x3A, 0x00, 0x40}, 3u, 13},
        {"LD (nn),A", {0x32, 0x00, 0x40}, 3u, 13},
        {"LD BC,nn", {0x01, 0x34, 0x12}, 3u, 10},
        {"LD HL,(nn)", {0x2A, 0x00, 0x40}, 3u, 16},
        {"LD (nn),HL", {0x22, 0x00, 0x40}, 3u, 16},
        {"LD SP,HL", {0xF9}, 1u, 6},
        {"INC BC", {0x03}, 1u, 6},
        {"DEC BC", {0x0B}, 1u, 6},
        {"INC B", {0x04}, 1u, 4},
        {"INC (HL)", {0x34}, 1u, 11},
        {"DEC (HL)", {0x35}, 1u, 11},
        {"ADD HL,BC", {0x09}, 1u, 11},
        {"ADD A,B", {0x80}, 1u, 4},
        {"ADD A,(HL)", {0x86}, 1u, 7},
        {"ADD A,n", {0xC6, 0x01}, 2u, 7},
        {"RLCA", {0x07}, 1u, 4},
        {"DAA", {0x27}, 1u, 4},
        {"SCF", {0x37}, 1u, 4},
        {"CCF", {0x3F}, 1u, 4},
        {"JP nn", {0xC3, 0x00, 0x00}, 3u, 10},
        {"JP (HL)", {0xE9}, 1u, 4},
        {"JR d", {0x18, 0x00}, 2u, 12},
        {"EX DE,HL", {0xEB}, 1u, 4},
        {"EX AF,AF'", {0x08}, 1u, 4},
        {"EXX", {0xD9}, 1u, 4},
        {"DI", {0xF3}, 1u, 4},
        {"EI", {0xFB}, 1u, 4},
        {"HALT", {0x76}, 1u, 4},
    };

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i)
    {
        z80_t *cpu = boot(cases[i].program, cases[i].length);
        z80_pins_t pins = {0};

        z80_set(cpu, Z80_REG_HL, 0x4000);
        z80_set(cpu, Z80_REG_BC, 0x4002);
        z80_set(cpu, Z80_REG_SP, 0x8000);

        const int tstates = run_one(cpu, &pins);
        CHECK(cases[i].expect == tstates, "%s should take %d T-states, took %d", cases[i].name, cases[i].expect,
              tstates);

        z80_free(cpu);
    }
}

/* ---------------------------------------------------------------- */
/* The CB set                                                        */
/* ---------------------------------------------------------------- */

/**
 * The CB shifts test their result, so S, Z and parity all move - which is what
 * separates them from RLCA and friends, whose opcodes do nearly the same work
 * and leave those three alone.
 */
static void test_cb_shifts(void)
{
    static const struct
    {
        const char *name;
        uint8_t opcode;
        uint8_t before;
        uint8_t expect;
        uint8_t expect_f;
    } cases[] = {
        {"RLC B", 0x00, 0x85, 0x0B, 0x09},
        {"RRC B", 0x08, 0x01, 0x80, 0x81},
        {"SLA B", 0x20, 0x80, 0x00, 0x45},
        {"SRA B keeps the sign", 0x28, 0x80, 0xC0, 0x84},
        {"SRL B", 0x38, 0x01, 0x00, 0x45},
        /* SLL is undocumented: shift left, and a 1 comes in at the bottom */
        {"SLL B", 0x30, 0x00, 0x01, 0x00},
    };

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i)
    {
        const uint8_t program[] = {0xCB, cases[i].opcode};
        z80_t *cpu = boot(program, sizeof program);
        z80_pins_t pins = {0};

        z80_set(cpu, Z80_REG_AF, 0x0000);
        z80_set(cpu, Z80_REG_BC, (uint16_t)((uint16_t)cases[i].before << 8));

        const int tstates = run_prefixed(cpu, &pins);
        const uint8_t got = (uint8_t)(z80_get(cpu, Z80_REG_BC) >> 8);

        CHECK(8 == tstates, "%s should take 8 T-states, took %d", cases[i].name, tstates);
        CHECK(cases[i].expect == got, "%s: B should be %02X, is %02X", cases[i].name, cases[i].expect, got);
        CHECK(cases[i].expect_f == reg_f(cpu), "%s: F should be %02X, is %02X", cases[i].name, cases[i].expect_f,
              reg_f(cpu));

        z80_free(cpu);
    }
}

/** BIT reports in Z and in P/V together, and takes X and Y from the operand. */
static void test_cb_bit(void)
{
    static const struct
    {
        const char *name;
        uint8_t opcode;
        uint8_t before;
        uint8_t expect_f;
    } cases[] = {
        {"BIT 7,B set", 0x78, 0x80, 0x90},
        {"BIT 7,B clear", 0x78, 0x00, 0x54},
        {"BIT 3,B set", 0x58, 0x28, 0x38},
        /* FE has bits 3 and 5 set, so X and Y come through from the operand */
        {"BIT 0,B clear", 0x40, 0xFE, 0x7C},
    };

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i)
    {
        const uint8_t program[] = {0xCB, cases[i].opcode};
        z80_t *cpu = boot(program, sizeof program);
        z80_pins_t pins = {0};

        z80_set(cpu, Z80_REG_AF, 0x0000);
        z80_set(cpu, Z80_REG_BC, (uint16_t)((uint16_t)cases[i].before << 8));
        (void)run_prefixed(cpu, &pins);

        CHECK(cases[i].expect_f == reg_f(cpu), "%s: F should be %02X, is %02X", cases[i].name, cases[i].expect_f,
              reg_f(cpu));
        CHECK(cases[i].before == (z80_get(cpu, Z80_REG_BC) >> 8), "%s must not change the operand", cases[i].name);

        z80_free(cpu);
    }
}

static void test_cb_set_and_res(void)
{
    static const uint8_t program[] = {
        0xCB, 0x80, /* RES 0,B */
        0xCB, 0xF8  /* SET 7,B */
    };

    z80_t *cpu = boot(program, sizeof program);
    z80_pins_t pins = {0};

    z80_set(cpu, Z80_REG_AF, 0x00FF); /* every flag set, and none may move */
    z80_set(cpu, Z80_REG_BC, 0x0700);

    (void)run_prefixed(cpu, &pins);
    CHECK(0x06 == (z80_get(cpu, Z80_REG_BC) >> 8), "RES 0,B gave %02X", (unsigned)(z80_get(cpu, Z80_REG_BC) >> 8));

    (void)run_prefixed(cpu, &pins);
    CHECK(0x86 == (z80_get(cpu, Z80_REG_BC) >> 8), "SET 7,B gave %02X", (unsigned)(z80_get(cpu, Z80_REG_BC) >> 8));
    CHECK(0xFF == reg_f(cpu), "RES and SET must not touch the flags, F is %02X", reg_f(cpu));

    z80_free(cpu);
}

/**
 * The memory forms read, modify and write - except BIT, which never writes and
 * is three T-states shorter for it.
 */
static void test_cb_memory(void)
{
    static const struct
    {
        const char *name;
        uint8_t opcode;
        uint8_t before;
        uint8_t expect;
        int expect_t;
    } cases[] = {
        {"RLC (HL)", 0x06, 0x85, 0x0B, 15},
        {"SET 0,(HL)", 0xC6, 0x00, 0x01, 15},
        {"RES 7,(HL)", 0xBE, 0xFF, 0x7F, 15},
        {"BIT 0,(HL)", 0x46, 0x01, 0x01, 12},
    };

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i)
    {
        const uint8_t program[] = {0xCB, cases[i].opcode};
        z80_t *cpu = boot(program, sizeof program);
        z80_pins_t pins = {0};

        world.memory[0x4000] = cases[i].before;
        z80_set(cpu, Z80_REG_HL, 0x4000);

        const int tstates = run_prefixed(cpu, &pins);

        CHECK(cases[i].expect_t == tstates, "%s should take %d T-states, took %d", cases[i].name, cases[i].expect_t,
              tstates);
        CHECK(cases[i].expect == world.memory[0x4000], "%s left %02X in memory, should be %02X", cases[i].name,
              world.memory[0x4000], cases[i].expect);

        z80_free(cpu);
    }
}

/** A prefix costs a whole M1 cycle, so R is incremented twice, not once. */
static void test_prefix_increments_refresh_twice(void)
{
    static const uint8_t program[] = {0xCB, 0x00}; /* RLC B */

    z80_t *cpu = boot(program, sizeof program);
    z80_pins_t pins = {0};

    z80_set(cpu, Z80_REG_IR, 0x0000);
    (void)run_prefixed(cpu, &pins);

    CHECK(2 == (z80_get(cpu, Z80_REG_IR) & 0xFFu), "R should have reached 2, is %02X",
          (unsigned)(z80_get(cpu, Z80_REG_IR) & 0xFFu));
    CHECK(0x0002 == z80_get(cpu, Z80_REG_PC), "PC should be past both bytes, is %04X", z80_get(cpu, Z80_REG_PC));

    z80_free(cpu);
}

static void test_every_cb_opcode_is_implemented(void)
{
    for (unsigned opcode = 0; opcode < 0x100u; ++opcode)
    {
        const uint8_t program[] = {0xCB, (uint8_t)opcode};
        z80_t *cpu = boot(program, sizeof program);
        z80_pins_t pins = {0};

        z80_set(cpu, Z80_REG_HL, 0x4000);
        (void)run_prefixed(cpu, &pins);

        CHECK(0 == z80_unimplemented(cpu), "CB %02X is not implemented", opcode);

        z80_free(cpu);
    }
}

/** Nothing in the unprefixed set should reach the unimplemented counter. */
static void test_every_unprefixed_opcode_is_implemented(void)
{
    static const uint8_t prefixes[] = {0xDD, 0xED, 0xFD};

    for (unsigned opcode = 0; opcode < 0x100u; ++opcode)
    {
        bool is_prefix = false;
        for (size_t i = 0; i < sizeof prefixes; ++i)
        {
            is_prefix = is_prefix || (prefixes[i] == opcode);
        }

        /* two of them, so a stray fetch of the operand cannot be mistaken for
           the instruction itself being missing */
        const uint8_t program[] = {(uint8_t)opcode, 0x00, 0x00, 0x00};
        z80_t *cpu = boot(program, sizeof program);
        z80_pins_t pins = {0};

        z80_set(cpu, Z80_REG_SP, 0x8000);
        (void)run_one(cpu, &pins);

        if (is_prefix)
        {
            CHECK(z80_unimplemented(cpu) > 0, "prefix %02X should still be counted as unimplemented", opcode);
        }
        else
        {
            CHECK(0 == z80_unimplemented(cpu), "opcode %02X is not implemented", opcode);
        }

        z80_free(cpu);
    }
}

/**
 * A snapshot taken between two edges must restore to the same place. Phase 3
 * added a conditional cycle count, which is engine state a naive snapshot
 * would drop - and the machine would then run the wrong number of T-states.
 */
static void test_snapshot_mid_instruction(void)
{
    static const uint8_t program[] = {
        0x31, 0x00, 0x80, /* LD SP,8000 */
        0xCD, 0x00, 0x01  /* CALL 0100  */
    };

    z80_t *cpu = boot(program, sizeof program);
    z80_pins_t pins = {0};
    world.memory[0x0100] = 0xC9;

    (void)run_one(cpu, &pins);

    /* stop partway through the CALL, where several cycles are still to come */
    for (int i = 0; i < 15; ++i)
    {
        clock_edge(cpu, &pins);
    }

    void *buffer = malloc(z80_snapshot_size());
    CHECK(NULL != buffer, "could not allocate a snapshot buffer");
    if (!buffer)
    {
        z80_free(cpu);
        return;
    }
    CHECK(z80_snapshot_size() == z80_save(cpu, buffer, z80_snapshot_size()), "z80_save did not fill the buffer");

    z80_t *restored = z80_new();
    CHECK(z80_load(restored, buffer, z80_snapshot_size()), "z80_load refused a snapshot it had just written");

    /* Clock both the same number of edges from the same point and compare the
       whole machine. Running to an instruction boundary would hide a snapshot
       that resumed a cycle early and caught up. */
    z80_pins_t restored_pins = pins;
    const int level_at_snapshot = world.level;

    for (int i = 0; i < 40; ++i)
    {
        clock_edge(cpu, &pins);
    }

    world.level = level_at_snapshot;
    for (int i = 0; i < 40; ++i)
    {
        clock_edge(restored, &restored_pins);
    }

    z80_state_t original_state;
    z80_state_t restored_state;
    z80_state(cpu, &original_state);
    z80_state(restored, &restored_state);

    CHECK(original_state.pc == restored_state.pc, "restored PC is %04X, should be %04X", restored_state.pc,
          original_state.pc);
    CHECK(original_state.sp == restored_state.sp, "restored SP is %04X, should be %04X", restored_state.sp,
          original_state.sp);
    CHECK(original_state.wz == restored_state.wz, "restored WZ is %04X, should be %04X", restored_state.wz,
          original_state.wz);
    CHECK(pins.ctrl == restored_pins.ctrl, "the restored machine drives %08X, should drive %08X", restored_pins.ctrl,
          pins.ctrl);
    CHECK(pins.A == restored_pins.A, "the restored machine addresses %04X, should address %04X", restored_pins.A,
          pins.A);

    free(buffer);
    z80_free(restored);
    z80_free(cpu);
}

int main(void)
{
    test_alu_flags();
    test_inc_dec_flags();
    test_daa();
    test_add_hl();
    test_memory_loads();
    test_push_pop();
    test_pop_af_restores_every_bit();
    test_ex_sp_hl();
    test_exchanges();
    test_call_and_return();
    test_rst();
    test_conditional_timing();
    test_djnz_loop();
    test_io_ports();
    test_iorq_is_distinct_from_mreq();
    test_instruction_timing();
    test_cb_shifts();
    test_cb_bit();
    test_cb_set_and_res();
    test_cb_memory();
    test_prefix_increments_refresh_twice();
    test_every_cb_opcode_is_implemented();
    test_every_unprefixed_opcode_is_implemented();
    test_snapshot_mid_instruction();

    if (failures)
    {
        printf("z80core instructions: %d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    printf("z80core instructions: all checks passed\n");
    return EXIT_SUCCESS;
}
