/**
 * @file   test_core.c
 * @brief  Behaviour the CPU core must have before any instruction exists
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
 *
 * This file is part of Z80core, released under the terms of the GNU General
 * Public License version 2. See LICENSE.md for the full text.
 */

#include "z80core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

/* the pins the CPU drives, which must not move while WAIT is held */
#define Z80_OUTPUTS_UNDER_TEST (Z80_M1 | Z80_MREQ | Z80_IORQ | Z80_RD | Z80_WR | Z80_RFSH)

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

/** A clock that does not move must not move the CPU (spec 4.2). */
static void test_stalled_clock_does_not_advance(void)
{
    z80_t *cpu = z80_new();
    z80_pins_t pins = {0};

    (void)z80_tick(cpu, &pins, 1);
    const uint64_t after_first = z80_edges(cpu);

    for (int i = 0; i < 10; ++i)
    {
        const uint32_t changed = z80_tick(cpu, &pins, 1);
        CHECK(changed == 0, "repeating the same clock level reported a change");
    }
    CHECK(z80_edges(cpu) == after_first, "repeating the same clock level advanced the CPU");

    (void)z80_tick(cpu, &pins, 0);
    CHECK(z80_edges(cpu) == after_first + 1, "a real edge did not advance the CPU");

    z80_free(cpu);
}

/** Both ways of advancing must agree, or the benchmark compares nothing. */
static void test_tick_and_run_agree(void)
{
    const uint64_t edges = 4096;

    z80_t *stepped = z80_new();
    z80_pins_t stepped_pins = {0};
    for (uint64_t i = 0; i < edges; ++i)
    {
        /* the clock starts low, so the first edge is a rising one */
        (void)z80_tick(stepped, &stepped_pins, (int)((i + 1u) & 1u));
    }

    z80_t *batched = z80_new();
    z80_pins_t batched_pins = {0};
    (void)z80_run(batched, &batched_pins, edges);

    CHECK(z80_edges(stepped) == z80_edges(batched), "edge counts differ: %llu vs %llu",
          (unsigned long long)z80_edges(stepped), (unsigned long long)z80_edges(batched));
    CHECK(stepped_pins.A == batched_pins.A, "address buses differ: %04X vs %04X", stepped_pins.A, batched_pins.A);
    CHECK(stepped_pins.ctrl == batched_pins.ctrl, "control pins differ: %08X vs %08X", stepped_pins.ctrl,
          batched_pins.ctrl);

    z80_free(stepped);
    z80_free(batched);
}

/** The mask must describe what actually moved, no more and no less. */
static void test_changed_mask_matches_pins(void)
{
    z80_t *cpu = z80_new();
    z80_pins_t pins = {0};
    bool saw_change = false;

    for (uint64_t i = 0; i < 64; ++i)
    {
        const z80_pins_t before = pins;
        const uint32_t changed = z80_tick(cpu, &pins, (int)(i & 1u));

        const uint32_t ctrl_moved = (pins.ctrl ^ before.ctrl);
        CHECK((changed & ctrl_moved) == ctrl_moved, "edge %llu: a control pin moved without being reported",
              (unsigned long long)i);
        CHECK(((changed & Z80_CHANGED_A) != 0) == (pins.A != before.A), "edge %llu: address change misreported",
              (unsigned long long)i);

        if (changed != 0)
        {
            saw_change = true;
        }
    }
    CHECK(saw_change, "no pin ever changed across a full fetch, the engine is not running");

    z80_free(cpu);
}

/** Reset must return the machine to its starting state, counter included. */
static void test_reset_clears_state(void)
{
    z80_t *cpu = z80_new();
    z80_pins_t pins = {0};

    (void)z80_run(cpu, &pins, 100);
    CHECK(z80_edges(cpu) == 100, "edge counter did not reach 100");

    z80_reset(cpu);
    CHECK(z80_edges(cpu) == 0, "reset left the edge counter at %llu", (unsigned long long)z80_edges(cpu));

    z80_free(cpu);
}

/**
 * Run the CPU against a small memory, the way any host does. Returns the
 * number of instruction fetches that completed.
 */
static int run_program(z80_t *cpu, const uint8_t *memory, size_t size, int edges)
{
    z80_pins_t pins = {0};
    int fetches = 0;

    for (int i = 0; i < edges; ++i)
    {
        const bool was_m1 = (pins.ctrl & Z80_M1) != 0;
        (void)z80_tick(cpu, &pins, (i + 1) & 1);

        if ((pins.ctrl & Z80_MREQ) && (pins.ctrl & Z80_RD))
        {
            pins.D = (pins.A < size) ? memory[pins.A] : 0x00;
        }
        if (!was_m1 && (pins.ctrl & Z80_M1))
        {
            ++fetches;
        }
    }
    return fetches;
}

/** NOP costs one fetch and advances PC by one, and nothing else happens. */
static void test_nop_advances_pc(void)
{
    static const uint8_t program[] = {0x00, 0x00, 0x00, 0x00};
    z80_t *cpu = z80_new();

    /* four fetches of four T-states: 32 edges */
    (void)run_program(cpu, program, sizeof program, 32);

    CHECK(4 == z80_get(cpu, Z80_REG_PC), "PC should be 4 after four NOPs, is %04X", z80_get(cpu, Z80_REG_PC));
    CHECK(0 == z80_unimplemented(cpu), "NOP was treated as unimplemented");

    z80_free(cpu);
}

/** R counts fetches, and bit 7 is not part of the counter. */
static void test_refresh_counts_fetches(void)
{
    static const uint8_t program[] = {0x00, 0x00, 0x00, 0x00};
    z80_t *cpu = z80_new();

    z80_set(cpu, Z80_REG_IR, 0x40FF); /* I = 0x40, R = 0xFF */
    (void)run_program(cpu, program, sizeof program, 8);

    const uint16_t ir = z80_get(cpu, Z80_REG_IR);
    CHECK(0x40 == (ir >> 8), "I should be untouched, is %02X", ir >> 8);
    CHECK(0x80 == (ir & 0xFF), "R should wrap 7F->00 keeping bit 7, is %02X", ir & 0xFF);

    z80_free(cpu);
}

/** HALT stops the program counter but not the clock. */
static void test_halt_holds_pc_and_asserts_the_pin(void)
{
    static const uint8_t program[] = {0x76, 0x00, 0x00, 0x00};
    z80_t *cpu = z80_new();
    z80_pins_t pins = {0};

    for (int i = 0; i < 64; ++i)
    {
        (void)z80_tick(cpu, &pins, (i + 1) & 1);
        if ((pins.ctrl & Z80_MREQ) && (pins.ctrl & Z80_RD))
        {
            pins.D = (pins.A < sizeof program) ? program[pins.A] : 0x00;
        }
    }

    z80_state_t state;
    z80_state(cpu, &state);

    CHECK(state.halted, "the CPU did not halt");
    CHECK(0 != (pins.ctrl & Z80_HALT), "the HALT pin is not asserted");
    /* PC stays *on* the HALT, not past it: a halted Z80 keeps fetching this
       same opcode, which is what the refresh cycles below are attached to.
       Leaving PC at 1 would re-fetch whatever follows the HALT instead. */
    CHECK(0 == state.pc, "PC should sit on the HALT at 0000, is %04X", state.pc);
    CHECK(state.edges >= 64, "the clock stopped along with the program counter");

    z80_free(cpu);
}

/**
 * The instruction set is complete, so the counter that made the gaps visible
 * should now never move. It is kept as the alarm it always was: if a future
 * change leaves an encoding unreachable, this is what says so.
 */
static void test_nothing_is_unimplemented(void)
{
    /* one of each prefix, which is where the gaps were longest */
    static const uint8_t program[] = {0xDD, 0x00, 0xFD, 0x00, 0xED, 0x00, 0xCB, 0x00};
    z80_t *cpu = z80_new();

    (void)run_program(cpu, program, sizeof program, 200);

    CHECK(0 == z80_unimplemented(cpu), "%llu opcode(s) reached the unimplemented counter",
          (unsigned long long)z80_unimplemented(cpu));

    z80_free(cpu);
}

/** LD A,n: fetch plus a memory read, and the byte lands in A. */
static void test_load_immediate(void)
{
    static const uint8_t program[] = {0x3E, 0x55, 0x06, 0x99}; /* LD A,55 ; LD B,99 */
    z80_t *cpu = z80_new();

    /* two instructions of 7 T-states: 28 edges */
    (void)run_program(cpu, program, sizeof program, 28);

    CHECK(0x55 == (z80_get(cpu, Z80_REG_AF) >> 8), "A should be 55, is %02X", z80_get(cpu, Z80_REG_AF) >> 8);
    CHECK(0x99 == (z80_get(cpu, Z80_REG_BC) >> 8), "B should be 99, is %02X", z80_get(cpu, Z80_REG_BC) >> 8);
    CHECK(4 == z80_get(cpu, Z80_REG_PC), "PC should be 4, is %04X", z80_get(cpu, Z80_REG_PC));
    CHECK(0 == z80_unimplemented(cpu), "LD r,n was treated as unimplemented");

    z80_free(cpu);
}

/** LD r,r' copies between registers and costs only its fetch. */
static void test_load_register_to_register(void)
{
    static const uint8_t program[] = {0x78, 0x00}; /* LD A,B */
    z80_t *cpu = z80_new();

    z80_set(cpu, Z80_REG_BC, 0x4200); /* B = 0x42 */
    (void)run_program(cpu, program, sizeof program, 8);

    CHECK(0x42 == (z80_get(cpu, Z80_REG_AF) >> 8), "A should have taken B's 42, is %02X",
          z80_get(cpu, Z80_REG_AF) >> 8);

    z80_free(cpu);
}

/** LD A,(HL) reads memory at HL. */
static void test_load_from_memory(void)
{
    static const uint8_t program[] = {0x7E, 0x00, 0x00, 0xAB}; /* LD A,(HL) with HL = 3 */
    z80_t *cpu = z80_new();

    z80_set(cpu, Z80_REG_HL, 3);
    (void)run_program(cpu, program, sizeof program, 14);

    CHECK(0xAB == (z80_get(cpu, Z80_REG_AF) >> 8), "A should be AB from memory, is %02X",
          z80_get(cpu, Z80_REG_AF) >> 8);

    z80_free(cpu);
}

/** LD (HL),r drives a write cycle the host can see and latch. */
static void test_store_to_memory(void)
{
    static const uint8_t program[] = {0x77, 0x00, 0x00, 0x00}; /* LD (HL),A */
    uint8_t memory[8];
    memcpy(memory, program, sizeof program);
    memset(memory + 4, 0, 4);

    z80_t *cpu = z80_new();
    z80_pins_t pins = {0};

    z80_set(cpu, Z80_REG_HL, 5);
    z80_set(cpu, Z80_REG_AF, 0x7F00); /* A = 0x7F */

    bool wrote = false;
    for (int i = 0; i < 16; ++i)
    {
        (void)z80_tick(cpu, &pins, (i + 1) & 1);
        if ((pins.ctrl & Z80_MREQ) && (pins.ctrl & Z80_RD))
        {
            pins.D = memory[pins.A & 7];
        }
        if ((pins.ctrl & Z80_MREQ) && (pins.ctrl & Z80_WR))
        {
            memory[pins.A & 7] = pins.D;
            wrote = true;
        }
    }

    CHECK(wrote, "LD (HL),A never asserted a write");
    CHECK(0x7F == memory[5], "memory at 5 should be 7F, is %02X", memory[5]);

    z80_free(cpu);
}

/** JP nn reads two bytes and continues from there. */
static void test_jump(void)
{
    static const uint8_t program[] = {0xC3, 0x34, 0x12}; /* JP 1234 */
    z80_t *cpu = z80_new();

    (void)run_program(cpu, program, sizeof program, 24);

    CHECK(0x1234 == z80_get(cpu, Z80_REG_PC), "PC should be 1234, is %04X", z80_get(cpu, Z80_REG_PC));

    z80_free(cpu);
}

/** WAIT stretches a cycle: the CPU holds its pins and makes no progress. */
static void test_wait_stretches_a_cycle(void)
{
    static const uint8_t program[] = {0x00, 0x00, 0x00, 0x00};

    z80_t *held = z80_new();
    z80_pins_t pins = {0};

    /* run to the point where WAIT is sampled, then hold it */
    for (int i = 0; i < 4; ++i)
    {
        (void)z80_tick(held, &pins, (i + 1) & 1);
        if ((pins.ctrl & Z80_MREQ) && (pins.ctrl & Z80_RD))
        {
            pins.D = program[pins.A & 3];
        }
    }

    pins.ctrl |= Z80_WAIT;

    /* WAIT is honoured where the cycle samples it, so the core runs on until
       it reaches that edge and stalls there - it does not freeze mid-cycle.
       Give it a whole machine cycle to get there. */
    for (int i = 0; i < 16; ++i)
    {
        (void)z80_tick(held, &pins, i & 1);
        if ((pins.ctrl & Z80_MREQ) && (pins.ctrl & Z80_RD))
        {
            pins.D = program[pins.A & 3];
        }
    }

    const uint16_t address_when_held = pins.A;
    const uint32_t control_when_held = pins.ctrl;
    const uint16_t pc_when_held = z80_get(held, Z80_REG_PC);

    for (int i = 0; i < 40; ++i)
    {
        (void)z80_tick(held, &pins, i & 1);
        CHECK(pins.A == address_when_held, "the address moved while WAIT was held");
        CHECK((pins.ctrl & Z80_OUTPUTS_UNDER_TEST) == (control_when_held & Z80_OUTPUTS_UNDER_TEST),
              "a control pin moved while WAIT was held");
    }
    CHECK(pc_when_held == z80_get(held, Z80_REG_PC), "the program counter moved while WAIT was held");

    /* releasing it lets the machine continue */
    pins.ctrl &= ~(uint32_t)Z80_WAIT;
    for (int i = 0; i < 40; ++i)
    {
        (void)z80_tick(held, &pins, (i + 1) & 1);
        if ((pins.ctrl & Z80_MREQ) && (pins.ctrl & Z80_RD))
        {
            pins.D = program[pins.A & 3];
        }
    }
    CHECK(z80_get(held, Z80_REG_PC) > 1, "the CPU did not resume after WAIT was released");

    z80_free(held);
}

/** A snapshot must restore mid-instruction, not merely between them. */
static void test_snapshot_restores_mid_instruction(void)
{
    static const uint8_t program[] = {0x00, 0x76, 0x00, 0x00};

    z80_t *original = z80_new();
    z80_pins_t pins = {0};

    /* stop deliberately part way through a fetch */
    for (int i = 0; i < 11; ++i)
    {
        (void)z80_tick(original, &pins, (i + 1) & 1);
        if ((pins.ctrl & Z80_MREQ) && (pins.ctrl & Z80_RD))
        {
            pins.D = program[pins.A & 3];
        }
    }

    uint8_t buffer[256];
    const size_t written = z80_save(original, buffer, sizeof buffer);
    CHECK(written > 0 && written == z80_snapshot_size(), "save wrote %zu bytes", written);

    z80_t *restored = z80_new();
    CHECK(z80_load(restored, buffer, written), "load rejected its own snapshot");

    /* both continue from the same place and must stay identical */
    z80_pins_t original_pins = pins;
    z80_pins_t restored_pins = pins;
    for (int i = 0; i < 40; ++i)
    {
        const uint32_t a = z80_tick(original, &original_pins, (i + 1) & 1);
        const uint32_t b = z80_tick(restored, &restored_pins, (i + 1) & 1);
        if ((original_pins.ctrl & Z80_MREQ) && (original_pins.ctrl & Z80_RD))
        {
            original_pins.D = program[original_pins.A & 3];
        }
        if ((restored_pins.ctrl & Z80_MREQ) && (restored_pins.ctrl & Z80_RD))
        {
            restored_pins.D = program[restored_pins.A & 3];
        }
        CHECK(a == b, "edge %d: changed masks diverge, %08X vs %08X", i, a, b);
        CHECK(original_pins.A == restored_pins.A, "edge %d: address diverges", i);
        CHECK(original_pins.ctrl == restored_pins.ctrl, "edge %d: control pins diverge", i);
    }

    CHECK(z80_get(original, Z80_REG_PC) == z80_get(restored, Z80_REG_PC), "PC diverged after restore");

    z80_free(original);
    z80_free(restored);
}

/** Rubbish must be refused rather than loaded as if it were a machine. */
static void test_snapshot_rejects_foreign_data(void)
{
    z80_t *cpu = z80_new();
    uint8_t rubbish[256];
    memset(rubbish, 0xA5, sizeof rubbish);

    CHECK(!z80_load(cpu, rubbish, sizeof rubbish), "a snapshot of rubbish was accepted");

    uint8_t buffer[256];
    CHECK(0 == z80_save(cpu, buffer, 4), "save into a buffer that is too small reported success");

    z80_free(cpu);
}

/** Registers must read back what was written, including the odd ones. */
static void test_registers_round_trip(void)
{
    z80_t *cpu = z80_new();

    for (int which = 0; which < Z80_REG_COUNT; ++which)
    {
        const uint16_t value = (uint16_t)(0x1234u + (unsigned)which);
        z80_set(cpu, (z80_reg)which, value);
        CHECK(value == z80_get(cpu, (z80_reg)which), "%s did not read back", z80_reg_name((z80_reg)which));
    }

    z80_free(cpu);
}

/**
 * A host asserting BUSRQ gets the bus at the end of the current machine cycle,
 * and the CPU picks up exactly where it left off afterwards.
 */
static void test_bus_request_releases_and_resumes(void)
{
    static const uint8_t program[] = {0x21, 0x34, 0x12, 0x00}; /* LD HL,1234 */

    z80_t *cpu = z80_new();
    z80_pins_t pins = {0};

    /* run into the middle of the instruction, then ask for the bus */
    for (int i = 0; i < 9; ++i)
    {
        (void)z80_tick(cpu, &pins, (i + 1) & 1);
        if ((pins.ctrl & Z80_MREQ) && (pins.ctrl & Z80_RD))
        {
            pins.D = (pins.A < sizeof program) ? program[pins.A] : 0x00;
        }
    }

    pins.ctrl |= Z80_BUSRQ;

    bool acknowledged = false;
    for (int i = 9; i < 40; ++i)
    {
        (void)z80_tick(cpu, &pins, (i + 1) & 1);
        if (pins.ctrl & Z80_BUSAK)
        {
            acknowledged = true;
            break;
        }
        if ((pins.ctrl & Z80_MREQ) && (pins.ctrl & Z80_RD))
        {
            pins.D = (pins.A < sizeof program) ? program[pins.A] : 0x00;
        }
    }
    CHECK(acknowledged, "BUSRQ was never acknowledged");

    const uint32_t driven = pins.ctrl & (Z80_M1 | Z80_MREQ | Z80_IORQ | Z80_RD | Z80_WR | Z80_RFSH);
    CHECK(0 == driven, "the CPU is still driving %08X while the bus is released", driven);

    /* hold it, and nothing must move */
    const uint16_t held_pc = z80_get(cpu, Z80_REG_PC);
    for (int i = 0; i < 20; ++i)
    {
        (void)z80_tick(cpu, &pins, i & 1);
        CHECK(0 != (pins.ctrl & Z80_BUSAK), "BUSAK dropped while BUSRQ was still held");
    }
    CHECK(held_pc == z80_get(cpu, Z80_REG_PC), "the CPU advanced while the bus was released");

    /* give it back, and the instruction must finish correctly */
    pins.ctrl &= ~(uint32_t)Z80_BUSRQ;
    for (int i = 0; i < 40; ++i)
    {
        (void)z80_tick(cpu, &pins, i & 1);
        if ((pins.ctrl & Z80_MREQ) && (pins.ctrl & Z80_RD))
        {
            pins.D = (pins.A < sizeof program) ? program[pins.A] : 0x00;
        }
    }

    CHECK(0 == (pins.ctrl & Z80_BUSAK), "BUSAK is still asserted after BUSRQ was released");
    CHECK(0x1234 == z80_get(cpu, Z80_REG_HL), "the interrupted instruction gave HL=%04X, should be 1234",
          z80_get(cpu, Z80_REG_HL));

    z80_free(cpu);
}

/* ---------------------------------------------------------------- */
/* Interrupts                                                        */
/* ---------------------------------------------------------------- */

/*
 * Nothing else checks any of this. FUSE has no way to assert a pin and
 * SingleStepTests has no interrupt state in its cases, so these tests are the
 * only thing standing between the interrupt logic and wishful thinking - which
 * is what issue #57, the hardware rig, exists to fix.
 */

static uint8_t interrupt_memory[0x10000];
static int interrupt_level;

static void interrupt_edge(z80_t *cpu, z80_pins_t *pins)
{
    interrupt_level ^= 1;
    (void)z80_tick(cpu, pins, interrupt_level);

    if (pins->ctrl & Z80_MREQ)
    {
        if (pins->ctrl & Z80_RD)
        {
            pins->D = interrupt_memory[pins->A];
        }
        else if (pins->ctrl & Z80_WR)
        {
            interrupt_memory[pins->A] = pins->D;
        }
    }
    if ((pins->ctrl & Z80_IORQ) && (pins->ctrl & Z80_M1))
    {
        pins->D = 0xFF; /* what an undriven bus gives during an acknowledge */
    }
}

/** Run one instruction, or one interrupt sequence, and return its T-states. */
static int interrupt_step(z80_t *cpu, z80_pins_t *pins)
{
    int edges = 1;

    if (0 == (pins->ctrl & Z80_M1))
    {
        interrupt_edge(cpu, pins);
    }
    for (int i = 0; i < 200; ++i)
    {
        const bool was_m1 = 0 != (pins->ctrl & Z80_M1);
        interrupt_edge(cpu, pins);
        ++edges;
        if (!was_m1 && (pins->ctrl & Z80_M1))
        {
            return (edges - 1) / 2;
        }
    }
    return -1;
}

static z80_t *interrupt_boot(const uint8_t *program, size_t size)
{
    memset(interrupt_memory, 0, sizeof interrupt_memory);
    memcpy(interrupt_memory, program, size);
    interrupt_level = 0;

    z80_t *cpu = z80_new();
    z80_set(cpu, Z80_REG_SP, 0x8000);
    return cpu;
}

/** NMI cannot be masked, vectors to 0066, and costs eleven T-states. */
static void test_nmi(void)
{
    static const uint8_t program[] = {0x00, 0x00, 0x00, 0x00}; /* NOPs */

    z80_t *cpu = interrupt_boot(program, sizeof program);
    z80_pins_t pins = {0};

    (void)interrupt_step(cpu, &pins); /* one NOP, so PC is 1 */

    /* Asserting the pin does not stop the instruction already running:
       acceptance happens at a boundary, so that one finishes first. */
    pins.ctrl |= Z80_NMI;
    const int in_flight = interrupt_step(cpu, &pins);
    pins.ctrl &= ~(uint32_t)Z80_NMI;
    CHECK(4 == in_flight, "the instruction in flight should have finished normally, took %d", in_flight);

    const int tstates = interrupt_step(cpu, &pins);

    z80_state_t state;
    z80_state(cpu, &state);

    CHECK(11 == tstates, "an NMI should take 11 T-states, took %d", tstates);
    CHECK(0x0066 == state.pc, "NMI should vector to 0066, went to %04X", state.pc);
    CHECK(0x7FFE == state.sp, "NMI should have pushed a return address, SP is %04X", state.sp);
    CHECK(0x02 == interrupt_memory[0x7FFE] && 0x00 == interrupt_memory[0x7FFF],
          "the return address should be 0002, is %02X%02X", interrupt_memory[0x7FFF], interrupt_memory[0x7FFE]);
    CHECK(!state.iff1, "NMI should have cleared IFF1");

    z80_free(cpu);
}

/** NMI is edge triggered: holding the pin must not interrupt repeatedly. */
static void test_nmi_is_edge_triggered(void)
{
    static const uint8_t program[] = {0x00, 0x00, 0x00, 0x00};

    z80_t *cpu = interrupt_boot(program, sizeof program);
    z80_pins_t pins = {0};
    interrupt_memory[0x0066] = 0x00; /* the handler is a NOP */

    (void)interrupt_step(cpu, &pins);
    pins.ctrl |= Z80_NMI; /* and never released */

    int taken = 0;
    for (int i = 0; i < 6; ++i)
    {
        (void)interrupt_step(cpu, &pins);
        if (0x0067 == z80_get(cpu, Z80_REG_PC) || 0x0066 == z80_get(cpu, Z80_REG_PC))
        {
            ++taken;
        }
    }

    CHECK(taken <= 2, "a held NMI was taken repeatedly (%d times)", taken);
    CHECK(0x7FFE == z80_get(cpu, Z80_REG_SP), "a held NMI pushed more than once, SP is %04X", z80_get(cpu, Z80_REG_SP));

    z80_free(cpu);
}

/** Mode 1 vectors to 0038 and costs thirteen T-states. */
static void test_interrupt_mode_1(void)
{
    static const uint8_t program[] = {0xFB, 0x00, 0x00, 0x00}; /* EI ; NOP... */

    z80_t *cpu = interrupt_boot(program, sizeof program);
    z80_pins_t pins = {0};

    (void)interrupt_step(cpu, &pins); /* EI */
    pins.ctrl |= Z80_INT;

    /* the instruction after EI must run first: that is the delay */
    const int after_ei = interrupt_step(cpu, &pins);
    CHECK(4 == after_ei, "the instruction after EI should be an ordinary NOP, took %d", after_ei);
    CHECK(0x0002 == z80_get(cpu, Z80_REG_PC), "an interrupt was taken during EI's grace instruction");

    const int tstates = interrupt_step(cpu, &pins);
    CHECK(13 == tstates, "mode 1 should take 13 T-states, took %d", tstates);
    CHECK(0x0038 == z80_get(cpu, Z80_REG_PC), "mode 1 should vector to 0038, went to %04X", z80_get(cpu, Z80_REG_PC));
    CHECK(0x02 == interrupt_memory[0x7FFE], "the return address should be 0002, low byte is %02X",
          interrupt_memory[0x7FFE]);

    z80_state_t state;
    z80_state(cpu, &state);
    CHECK(!state.iff1 && !state.iff2, "accepting an interrupt must clear both flip-flops");

    z80_free(cpu);
}

/** With interrupts disabled, INT is ignored however long it is held. */
static void test_interrupt_is_masked_by_di(void)
{
    static const uint8_t program[] = {0xF3, 0x00, 0x00, 0x00}; /* DI ; NOP... */

    z80_t *cpu = interrupt_boot(program, sizeof program);
    z80_pins_t pins = {0};

    (void)interrupt_step(cpu, &pins); /* DI */
    pins.ctrl |= Z80_INT;

    for (int i = 0; i < 4; ++i)
    {
        const int tstates = interrupt_step(cpu, &pins);
        CHECK(4 == tstates, "a masked INT disturbed the instruction stream (%d T-states)", tstates);
    }
    CHECK(0x8000 == z80_get(cpu, Z80_REG_SP), "a masked INT pushed something, SP is %04X", z80_get(cpu, Z80_REG_SP));

    z80_free(cpu);
}

/** Mode 2 reads the handler's address from a table I points at: nineteen. */
static void test_interrupt_mode_2(void)
{
    static const uint8_t program[] = {
        0xED, 0x5E, /* IM 2 */
        0xFB,       /* EI   */
        0x00, 0x00  /* NOP  */
    };

    z80_t *cpu = interrupt_boot(program, sizeof program);
    z80_pins_t pins = {0};

    /* the device supplies FF, so the entry is at I:FF */
    z80_set(cpu, Z80_REG_IR, 0x9000);
    interrupt_memory[0x90FF] = 0x34;
    interrupt_memory[0x9100] = 0x12;

    (void)interrupt_step(cpu, &pins); /* ED prefix */
    (void)interrupt_step(cpu, &pins); /* IM 2      */
    (void)interrupt_step(cpu, &pins); /* EI        */
    pins.ctrl |= Z80_INT;
    (void)interrupt_step(cpu, &pins); /* the grace instruction */

    const int tstates = interrupt_step(cpu, &pins);

    CHECK(19 == tstates, "mode 2 should take 19 T-states, took %d", tstates);
    CHECK(0x1234 == z80_get(cpu, Z80_REG_PC), "mode 2 should have jumped to 1234, went to %04X",
          z80_get(cpu, Z80_REG_PC));

    z80_free(cpu);
}

/**
 * An interrupt wakes a halted CPU, and the address it pushes must be the
 * instruction *after* the HALT - not the HALT itself, which PC sits on.
 */
static void test_interrupt_wakes_halt(void)
{
    static const uint8_t program[] = {0xFB, 0x76, 0x00, 0x00}; /* EI ; HALT */

    z80_t *cpu = interrupt_boot(program, sizeof program);
    z80_pins_t pins = {0};

    (void)interrupt_step(cpu, &pins); /* EI   */
    (void)interrupt_step(cpu, &pins); /* HALT */

    z80_state_t halted_state;
    z80_state(cpu, &halted_state);
    CHECK(halted_state.halted, "the CPU did not halt");
    CHECK(0x0001 == halted_state.pc, "PC should sit on the HALT at 0001, is %04X", halted_state.pc);

    pins.ctrl |= Z80_INT;
    (void)interrupt_step(cpu, &pins); /* the halt fetch in flight finishes first */
    (void)interrupt_step(cpu, &pins); /* then the interrupt is taken */

    z80_state_t state;
    z80_state(cpu, &state);

    CHECK(!state.halted, "the interrupt did not wake the CPU");
    CHECK(0 == (pins.ctrl & Z80_HALT), "the HALT pin is still asserted after waking");
    CHECK(0x0038 == state.pc, "the handler should be at 0038, PC is %04X", state.pc);
    CHECK(0x02 == interrupt_memory[0x7FFE], "the return address must be past the HALT, low byte is %02X",
          interrupt_memory[0x7FFE]);

    z80_free(cpu);
}

/** RESET must be held for three clocks, and then puts the machine back. */
static void test_reset_pin(void)
{
    static const uint8_t program[] = {0x00, 0x00, 0x00, 0x00};

    z80_t *cpu = interrupt_boot(program, sizeof program);
    z80_pins_t pins = {0};

    (void)interrupt_step(cpu, &pins);
    (void)interrupt_step(cpu, &pins);
    z80_set(cpu, Z80_REG_IR, 0x4455);
    CHECK(0 != z80_get(cpu, Z80_REG_PC), "the CPU never ran");

    /* a glitch shorter than three T-states must not take */
    pins.ctrl |= Z80_RESET;
    for (int i = 0; i < 4; ++i)
    {
        interrupt_edge(cpu, &pins);
    }
    pins.ctrl &= ~(uint32_t)Z80_RESET;
    CHECK(0x4455 == z80_get(cpu, Z80_REG_IR), "a two-T-state RESET glitch reset the machine");

    /* held properly, it does */
    pins.ctrl |= Z80_RESET;
    for (int i = 0; i < 10; ++i)
    {
        interrupt_edge(cpu, &pins);
    }
    pins.ctrl &= ~(uint32_t)Z80_RESET;

    z80_state_t state;
    z80_state(cpu, &state);
    CHECK(0 == state.pc, "RESET should clear PC, it is %04X", state.pc);
    CHECK(0 == state.i && 0 == state.r, "RESET should clear I and R, they are %02X %02X", state.i, state.r);
    CHECK(0 == state.im, "RESET should select mode 0, it is %u", state.im);
    CHECK(!state.iff1 && !state.iff2, "RESET should disable interrupts");

    z80_free(cpu);
}

/**
 * LD A,I reports IFF2 in P/V - and reports it clear if an interrupt is
 * accepted while the instruction is running, even though interrupts were on.
 * Code that saves the state around a critical section restores the wrong one.
 */
static void test_ld_a_i_interrupt_race(void)
{
    static const uint8_t program[] = {0xFB,       /* EI      */
                                      0x00,       /* NOP     */
                                      0xED, 0x57, /* LD A,I  */
                                      0x00};

    /* first without an interrupt: P/V must report that they are enabled */
    z80_t *cpu = interrupt_boot(program, sizeof program);
    z80_pins_t pins = {0};
    z80_set(cpu, Z80_REG_IR, 0x2000);

    (void)interrupt_step(cpu, &pins); /* EI  */
    (void)interrupt_step(cpu, &pins); /* NOP */
    (void)interrupt_step(cpu, &pins); /* ED  */
    (void)interrupt_step(cpu, &pins); /* LD A,I */

    CHECK(0 != (z80_get(cpu, Z80_REG_AF) & 0x04u), "P/V should report interrupts enabled");
    z80_free(cpu);

    /* now with INT held: the flip-flop is cleared out from under P/V */
    cpu = interrupt_boot(program, sizeof program);
    memset(&pins, 0, sizeof pins);
    z80_set(cpu, Z80_REG_IR, 0x2000);

    (void)interrupt_step(cpu, &pins); /* EI  */
    (void)interrupt_step(cpu, &pins); /* NOP */
    pins.ctrl |= Z80_INT;
    (void)interrupt_step(cpu, &pins); /* ED  */
    (void)interrupt_step(cpu, &pins); /* LD A,I, with the interrupt imminent */

    CHECK(0 == (z80_get(cpu, Z80_REG_AF) & 0x04u), "P/V should have come out clear: the race was not modelled");
    CHECK(0x20 == (z80_get(cpu, Z80_REG_AF) >> 8), "A should still have taken I, is %02X",
          (unsigned)(z80_get(cpu, Z80_REG_AF) >> 8));

    z80_free(cpu);
}

int main(void)
{
    test_stalled_clock_does_not_advance();
    test_tick_and_run_agree();
    test_changed_mask_matches_pins();
    test_reset_clears_state();
    test_nop_advances_pc();
    test_refresh_counts_fetches();
    test_halt_holds_pc_and_asserts_the_pin();
    test_nothing_is_unimplemented();
    test_bus_request_releases_and_resumes();
    test_nmi();
    test_nmi_is_edge_triggered();
    test_interrupt_mode_1();
    test_interrupt_is_masked_by_di();
    test_interrupt_mode_2();
    test_interrupt_wakes_halt();
    test_reset_pin();
    test_ld_a_i_interrupt_race();
    test_load_immediate();
    test_load_register_to_register();
    test_load_from_memory();
    test_store_to_memory();
    test_jump();
    test_wait_stretches_a_cycle();
    test_snapshot_restores_mid_instruction();
    test_snapshot_rejects_foreign_data();
    test_registers_round_trip();

    if (failures == 0)
    {
        printf("z80core: all checks passed\n");
        return EXIT_SUCCESS;
    }
    printf("z80core: %d check(s) failed\n", failures);
    return EXIT_FAILURE;
}
