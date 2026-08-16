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
    CHECK(1 == state.pc, "PC should stay at 1 while halted, is %04X", state.pc);
    CHECK(state.edges >= 64, "the clock stopped along with the program counter");

    z80_free(cpu);
}

/** An opcode with no implementation is counted, not silently ignored. */
static void test_unimplemented_opcodes_are_counted(void)
{
    static const uint8_t program[] = {0x3E, 0x3E, 0x3E, 0x3E};
    z80_t *cpu = z80_new();

    (void)run_program(cpu, program, sizeof program, 40);

    CHECK(z80_unimplemented(cpu) > 0, "an unimplemented opcode was not counted");

    z80_free(cpu);
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

int main(void)
{
    test_stalled_clock_does_not_advance();
    test_tick_and_run_agree();
    test_changed_mask_matches_pins();
    test_reset_clears_state();
    test_nop_advances_pc();
    test_refresh_counts_fetches();
    test_halt_holds_pc_and_asserts_the_pin();
    test_unimplemented_opcodes_are_counted();
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
