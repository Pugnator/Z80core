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

int main(void)
{
    test_stalled_clock_does_not_advance();
    test_tick_and_run_agree();
    test_changed_mask_matches_pins();
    test_reset_clears_state();

    if (failures == 0)
    {
        printf("z80core: all checks passed\n");
        return EXIT_SUCCESS;
    }
    printf("z80core: %d check(s) failed\n", failures);
    return EXIT_FAILURE;
}
