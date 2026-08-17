/**
 * @file   test_analyse.c
 * @brief  Telling code from data, and strings from both
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
 *
 * This file is part of Z80core, released under the terms of the GNU General
 * Public License version 2. See LICENSE.md for the full text.
 *
 * Written before the implementation, per the plan in issue #47. The corpus
 * cases at the end use the real 48K ROM, because a rule that works on
 * hand-written byte sequences and not on a real program has not been tested.
 */

#include "zdasm.h"

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

#define MAX_REGIONS 512
static zdasm_region regions[MAX_REGIONS];

/** The kind covering a given address, or -1 if nothing does. */
static int kind_at(size_t count, uint16_t address)
{
    for (size_t i = 0; i < count && i < MAX_REGIONS; ++i)
    {
        if (address >= regions[i].start && address < regions[i].start + regions[i].length)
        {
            return (int)regions[i].kind;
        }
    }
    return -1;
}

static size_t analyse(const uint8_t *code, size_t size, uint16_t entry)
{
    return zdasm_analyse(code, size, &entry, 1u, regions, MAX_REGIONS);
}

/* ---------------------------------------------------------------- */
/* Reachability                                                      */
/* ---------------------------------------------------------------- */

static void test_straight_line_then_a_table(void)
{
    /* three instructions, an unconditional return, then bytes nothing reaches */
    static const uint8_t code[] = {
        0x3E, 0x01,            /* 0000: LD A,1 */
        0x3C,                  /* 0002: INC A  */
        0xC9,                  /* 0003: RET    */
        0x11, 0x22, 0x33, 0x44 /* 0004: a table */
    };

    const size_t count = analyse(code, sizeof code, 0);

    CHECK(ZDASM_CODE == kind_at(count, 0x0000), "the entry point should be code");
    CHECK(ZDASM_CODE == kind_at(count, 0x0003), "the RET should be code");
    CHECK(ZDASM_CODE != kind_at(count, 0x0004), "the table after a RET is not reachable");
    CHECK(ZDASM_CODE != kind_at(count, 0x0007), "nor is the end of it");
}

static void test_a_backward_jump_terminates(void)
{
    /* JR -2 jumps to itself: the walk must notice it has been here */
    static const uint8_t code[] = {
        0x00,      /* 0000: NOP     */
        0x18, 0xFE /* 0001: JR 0001 */
    };

    const size_t count = analyse(code, sizeof code, 0);

    CHECK(count > 0, "the walk did not terminate, or produced nothing");
    CHECK(ZDASM_CODE == kind_at(count, 0x0001), "the loop should be code");
}

static void test_call_marks_the_target_and_continues(void)
{
    static const uint8_t code[] = {
        0xCD, 0x06, 0x00, /* 0000: CALL 0006 */
        0x3C,             /* 0003: INC A     */
        0xC9,             /* 0004: RET       */
        0xFF,             /* 0005: unreached */
        0xC9              /* 0006: RET       */
    };

    const size_t count = analyse(code, sizeof code, 0);

    CHECK(ZDASM_CODE == kind_at(count, 0x0006), "a call target should be reachable");
    CHECK(ZDASM_CODE == kind_at(count, 0x0003), "execution continues after a call");
    CHECK(ZDASM_CODE != kind_at(count, 0x0005), "the byte between them is reached by nothing");
}

static void test_unconditional_jump_does_not_fall_through(void)
{
    static const uint8_t code[] = {
        0xC3, 0x05, 0x00, /* 0000: JP 0005   */
        0xAA, 0xBB,       /* 0003: unreached */
        0xC9              /* 0005: RET       */
    };

    const size_t count = analyse(code, sizeof code, 0);

    CHECK(ZDASM_CODE == kind_at(count, 0x0005), "the jump target should be code");
    CHECK(ZDASM_CODE != kind_at(count, 0x0003), "nothing falls through an unconditional jump");
}

static void test_conditional_jump_reaches_both_ways(void)
{
    static const uint8_t code[] = {
        0xC2, 0x05, 0x00, /* 0000: JP NZ,0005  */
        0x3C,             /* 0003: INC A       */
        0xC9,             /* 0004: RET         */
        0xC9              /* 0005: RET         */
    };

    const size_t count = analyse(code, sizeof code, 0);

    CHECK(ZDASM_CODE == kind_at(count, 0x0003), "the fall-through of a conditional is reachable");
    CHECK(ZDASM_CODE == kind_at(count, 0x0005), "and so is its target");
}

static void test_extra_entry_points_are_honoured(void)
{
    static const uint8_t code[] = {
        0xC9,      /* 0000: RET       */
        0xFF,      /* 0001: unreached */
        0x3C, 0xC9 /* 0002: INC A ; RET, reached only by being told */
    };
    static const uint16_t entries[] = {0x0000, 0x0002};

    const size_t count = zdasm_analyse(code, sizeof code, entries, 2u, regions, MAX_REGIONS);

    CHECK(ZDASM_CODE == kind_at(count, 0x0002), "a supplied entry point should be traced");
    CHECK(ZDASM_CODE != kind_at(count, 0x0001), "and nothing else should become reachable");
}

/* ---------------------------------------------------------------- */
/* Classification                                                    */
/* ---------------------------------------------------------------- */

static void test_a_short_printable_run_is_not_a_string(void)
{
    static const uint8_t code[] = {0xC9,                 /* 0000: RET */
                                   'a',  'b', 'c', 0x00, /* a run shorter than the threshold */
                                   0x01, 0x02};

    const size_t count = analyse(code, sizeof code, 0);

    CHECK(ZDASM_STRING != kind_at(count, 0x0001), "three characters is not a string");
}

static void test_a_long_printable_run_is_a_string(void)
{
    static const uint8_t code[] = {0xC9, 'H', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd'};

    const size_t count = analyse(code, sizeof code, 0);

    CHECK(ZDASM_STRING == kind_at(count, 0x0001), "eleven characters should be a string");
    CHECK(ZDASM_STRING == kind_at(count, 0x000B), "including its last character");
}

/**
 * The classic false positive, taken from the real ROM: 2A 5D 5C is
 * LD HL,(5C5D), an everyday instruction, and it reads as printable text
 * because the Spectrum's system variables live at 0x5Cxx.
 */
static void test_the_ld_hl_false_positive(void)
{
    static const uint8_t code[] = {
        0x2A, 0x5D, 0x5C, /* 0000: LD HL,(5C5D) */
        0x2A, 0x5D, 0x5C, /* 0003: and again    */
        0xC9              /* 0006: RET          */
    };

    const size_t count = analyse(code, sizeof code, 0);

    CHECK(ZDASM_CODE == kind_at(count, 0x0000), "LD HL,(nn) is code, not text");
    CHECK(ZDASM_STRING != kind_at(count, 0x0001), "and must never be called a string");
}

static void test_unreachable_binary_is_data(void)
{
    static const uint8_t code[] = {0xC9, 0x00, 0x01, 0x02, 0x03, 0xFE, 0xFF};

    const size_t count = analyse(code, sizeof code, 0);

    CHECK(ZDASM_DATA == kind_at(count, 0x0002), "unreachable non-printable bytes are data");
}

/* ---------------------------------------------------------------- */
/* The shape of the answer                                           */
/* ---------------------------------------------------------------- */

static void test_regions_tile_the_buffer(void)
{
    static const uint8_t code[] = {0x3E, 0x01, 0xC9, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 0x00, 0x01};

    const size_t count = analyse(code, sizeof code, 0);

    CHECK(count > 0 && count <= MAX_REGIONS, "region count %zu is not usable", count);

    uint32_t expected_start = 0;
    for (size_t i = 0; i < count; ++i)
    {
        CHECK(regions[i].start == expected_start, "region %zu starts at %04X, expected %04X", i, regions[i].start,
              (unsigned)expected_start);
        CHECK(regions[i].length > 0, "region %zu is empty", i);
        if (i > 0)
        {
            /* Adjacent regions of one kind should have been merged - except
               strings, which are adjacent precisely because the first one was
               terminated, and merging them would hide the terminator. */
            CHECK(regions[i].kind != regions[i - 1].kind || ZDASM_STRING == regions[i].kind,
                  "regions %zu and %zu have the same kind and should be one", i - 1, i);
        }
        expected_start += regions[i].length;
    }
    CHECK(expected_start == sizeof code, "the regions cover %u bytes, the buffer is %u", (unsigned)expected_start,
          (unsigned)sizeof code);
}

static void test_truncation_is_reported_not_hidden(void)
{
    /* alternating code and data, so it needs far more regions than it is given */
    static uint8_t code[64];
    for (size_t i = 0; i < sizeof code; i += 4)
    {
        code[i] = 0xC9;     /* RET  */
        code[i + 1] = 0xFF; /* data */
        code[i + 2] = 0xFF;
        code[i + 3] = 0xFF;
    }

    /* Every RET needs its own entry point: control stops at the first one, so
       without this the buffer really is two regions and the test proves
       nothing. */
    static uint16_t entries[sizeof code / 4u];
    for (size_t i = 0; i < sizeof entries / sizeof entries[0]; ++i)
    {
        entries[i] = (uint16_t)(i * 4u);
    }

    const size_t full =
        zdasm_analyse(code, sizeof code, entries, sizeof entries / sizeof entries[0], regions, MAX_REGIONS);
    CHECK(full > 2u, "the buffer should need more than two regions, needs %zu", full);

    const size_t needed = zdasm_analyse(code, sizeof code, entries, sizeof entries / sizeof entries[0], regions, 2u);
    CHECK(needed == full, "truncated to 2 it reported %zu regions needed, should still be %zu", needed, full);
}

static void test_degenerate_buffers_do_not_crash(void)
{
    static const uint8_t one[] = {0x00};
    static const uint16_t entry = 0;

    CHECK(0 == zdasm_analyse(NULL, 0, &entry, 1u, regions, MAX_REGIONS), "a null buffer should produce nothing");
    CHECK(0 == zdasm_analyse(one, 0, &entry, 1u, regions, MAX_REGIONS), "an empty buffer should produce nothing");
    CHECK(1 == zdasm_analyse(one, 1, &entry, 1u, regions, MAX_REGIONS), "one byte should be one region");

    /* no entry points at all: everything is unreachable, and that is an answer */
    const size_t none = zdasm_analyse(one, 1, NULL, 0, regions, MAX_REGIONS);
    CHECK(none >= 1, "with no entry points the whole buffer should still be classified");

    /* counting without storing */
    CHECK(zdasm_analyse(one, 1, &entry, 1u, NULL, 0) >= 1, "a NULL output should still count");
}

static void test_random_bytes_terminate(void)
{
    static uint8_t noise[4096];
    uint32_t seed = 12345;
    for (size_t i = 0; i < sizeof noise; ++i)
    {
        seed = seed * 1103515245u + 12345u;
        noise[i] = (uint8_t)(seed >> 16);
    }

    const size_t count = analyse(noise, sizeof noise, 0);
    CHECK(count > 0, "analysing noise produced nothing, or did not return");
}

/* ---------------------------------------------------------------- */
/* The real ROM                                                      */
/* ---------------------------------------------------------------- */

static void test_the_spectrum_rom(const char *path)
{
    static uint8_t rom[0x4000];

    FILE *file = fopen(path, "rb");
    if (!file)
    {
        printf("skipping the ROM cases: %s not readable\n", path);
        return;
    }
    const size_t read = fread(rom, 1u, sizeof rom, file);
    fclose(file);

    if (sizeof rom != read)
    {
        printf("skipping the ROM cases: %s is %u bytes\n", path, (unsigned)read);
        return;
    }

    /* reset, the RST vectors, and the NMI - what a Spectrum actually enters at */
    static const uint16_t entries[] = {0x0000, 0x0008, 0x0010, 0x0018, 0x0020, 0x0028, 0x0030, 0x0038, 0x0066};
    const size_t count =
        zdasm_analyse(rom, sizeof rom, entries, sizeof entries / sizeof entries[0], regions, MAX_REGIONS);

    CHECK(count > 1, "the ROM came back as a single region, which cannot be right");

    CHECK(ZDASM_CODE == kind_at(count, 0x0000), "the reset vector should be code");
    CHECK(ZDASM_CODE == kind_at(count, 0x0038), "the interrupt handler should be code");

    /* "Start tape, then press any key", and the copyright message */
    CHECK(ZDASM_STRING == kind_at(count, 0x09A2), "0x09A2 should be a string");
    CHECK(ZDASM_STRING == kind_at(count, 0x153A), "0x153A should be a string");

    size_t code_bytes = 0;
    for (size_t i = 0; i < count && i < MAX_REGIONS; ++i)
    {
        if (ZDASM_CODE == regions[i].kind)
        {
            code_bytes += regions[i].length;
        }
    }

    /* A ROM that is nearly all code, or nearly none, means the walk is broken
       rather than that the ROM is unusual. */
    const size_t percent = (code_bytes * 100u) / sizeof rom;
    CHECK(percent > 30 && percent < 95, "%u%% of the ROM came back as code, which is not plausible", (unsigned)percent);
    printf("ROM: %u%% code across %zu regions\n", (unsigned)percent, count);
}

int main(int argc, char **argv)
{
    test_straight_line_then_a_table();
    test_a_backward_jump_terminates();
    test_call_marks_the_target_and_continues();
    test_unconditional_jump_does_not_fall_through();
    test_conditional_jump_reaches_both_ways();
    test_extra_entry_points_are_honoured();

    test_a_short_printable_run_is_not_a_string();
    test_a_long_printable_run_is_a_string();
    test_the_ld_hl_false_positive();
    test_unreachable_binary_is_data();

    test_regions_tile_the_buffer();
    test_truncation_is_reported_not_hidden();
    test_degenerate_buffers_do_not_crash();
    test_random_bytes_terminate();

    if (argc > 1)
    {
        test_the_spectrum_rom(argv[1]);
    }

    if (failures)
    {
        printf("zdasm analysis: %d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    printf("zdasm analysis: all checks passed\n");
    return EXIT_SUCCESS;
}
