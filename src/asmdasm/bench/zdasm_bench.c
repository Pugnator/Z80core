/**
 * @file   zdasm_bench.c
 * @brief  What decoding one instruction costs, and proof it stayed correct
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
 *
 * This file is part of Z80core, released under the terms of the GNU General
 * Public License version 2. See LICENSE.md for the full text.
 *
 * Two numbers and a checksum. The numbers are decode throughput and analysis
 * throughput over a real image; the checksum is a hash of everything the
 * decoder produced - text, lengths, and the control-flow answers - so a change
 * that makes decoding faster by making it wrong is caught here rather than
 * being reported as an improvement.
 *
 * The checksum is the point. Comparing timings across a change is only
 * meaningful if both runs did the same work, and "the tests still pass" does
 * not prove that at this resolution.
 */

#include "zdasm.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/** FNV-1a, chosen for being short to write and obviously order-sensitive. */
#define FNV_OFFSET 1469598103934665603ull
#define FNV_PRIME 1099511628211ull

static void hash_byte(uint64_t *state, uint8_t byte)
{
    *state ^= byte;
    *state *= FNV_PRIME;
}

static void hash_insn(uint64_t *state, const zdasm_insn *insn)
{
    for (const char *p = insn->text; *p; ++p)
    {
        hash_byte(state, (uint8_t)*p);
    }
    hash_byte(state, insn->length);
    hash_byte(state, (uint8_t)insn->known);
    hash_byte(state, (uint8_t)insn->branches);
    hash_byte(state, (uint8_t)insn->continues);
    hash_byte(state, (uint8_t)(insn->target & 0xFFu));
    hash_byte(state, (uint8_t)(insn->target >> 8));
}

static double seconds_since(clock_t start)
{
    const double elapsed = (double)(clock() - start) / (double)CLOCKS_PER_SEC;
    /* A pass too quick to time says nothing useful; report it as such rather
       than dividing by zero and printing an infinite rate. */
    return elapsed > 0.0 ? elapsed : 0.0;
}

/**
 * One line of measurement. Rates are printed in whatever unit keeps the
 * significant digits: a million instructions a second and a few hundred
 * analysis passes a second are both interesting, and the same scale factor
 * cannot show them both.
 */
static void report(const char *what, uint64_t items, const char *unit, double seconds)
{
    if (seconds <= 0.0)
    {
        printf("%-30s %16s   (%s)\n", what, "too fast to time", unit);
        return;
    }
    const double rate = (double)items / seconds;
    if (rate >= 1e6)
    {
        printf("%-30s %10.2f M %s/s   (%.3f s)\n", what, rate / 1e6, unit, seconds);
    }
    else
    {
        printf("%-30s %12.1f %s/s   (%.3f s)\n", what, rate, unit, seconds);
    }
}

/** Decode every instruction in the image once, hashing what came back. */
static uint64_t decode_pass(const uint8_t *image, size_t size, uint64_t *instructions)
{
    uint64_t state = FNV_OFFSET;
    uint64_t count = 0;

    for (uint32_t pc = 0; pc < size;)
    {
        zdasm_insn insn;
        const uint8_t used = zdasm_decode(image, size, (uint16_t)pc, &insn);
        if (!used)
        {
            break;
        }
        hash_insn(&state, &insn);
        ++count;
        pc += used;
    }

    *instructions = count;
    return state;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: zdasm_bench <image> [repetitions]\n");
        return EXIT_FAILURE;
    }

    unsigned repetitions = 200;
    if (argc > 2)
    {
        const long requested = atol(argv[2]);
        if (requested > 0)
        {
            repetitions = (unsigned)requested;
        }
    }

    FILE *in = fopen(argv[1], "rb");
    if (!in)
    {
        fprintf(stderr, "zdasm_bench: cannot open %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    if (0 != fseek(in, 0, SEEK_END))
    {
        fclose(in);
        fprintf(stderr, "zdasm_bench: cannot size %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    const long size = ftell(in);
    rewind(in);
    if (size <= 0 || size > 0x10000)
    {
        fclose(in);
        fprintf(stderr, "zdasm_bench: %s does not fit the 64K address space\n", argv[1]);
        return EXIT_FAILURE;
    }

    uint8_t *image = malloc((size_t)size);
    if (!image || 1 != fread(image, (size_t)size, 1, in))
    {
        free(image);
        fclose(in);
        fprintf(stderr, "zdasm_bench: cannot read %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    fclose(in);

    uint64_t instructions = 0;
    const uint64_t expected = decode_pass(image, (size_t)size, &instructions);

    printf("zdasm bench: %s, %ld bytes, %llu instructions, %u repetitions\n\n", argv[1], size,
           (unsigned long long)instructions, repetitions);

    /* Linear decode: what a listing costs, and the path zdasm_analyse spends
       nearly all of its time in. */
    clock_t start = clock();
    for (unsigned i = 0; i < repetitions; ++i)
    {
        uint64_t ignored = 0;
        if (decode_pass(image, (size_t)size, &ignored) != expected)
        {
            free(image);
            fprintf(stderr, "zdasm_bench: decoding is not deterministic\n");
            return EXIT_FAILURE;
        }
    }
    report("zdasm_decode() linear", instructions * repetitions, "insn", seconds_since(start));

    /* Reachability over the whole image: decode plus the work list, from the
       entry points a ROM really has. */
    static const uint16_t entries[] = {0x0000, 0x0008, 0x0010, 0x0018, 0x0020, 0x0028, 0x0030, 0x0038, 0x0066};
    const size_t regions = zdasm_analyse(image, (size_t)size, entries, sizeof entries / sizeof entries[0], NULL, 0);

    start = clock();
    for (unsigned i = 0; i < repetitions; ++i)
    {
        if (zdasm_analyse(image, (size_t)size, entries, sizeof entries / sizeof entries[0], NULL, 0) != regions)
        {
            free(image);
            fprintf(stderr, "zdasm_bench: analysis is not deterministic\n");
            return EXIT_FAILURE;
        }
    }
    report("zdasm_analyse() whole image", (uint64_t)repetitions, "pass", seconds_since(start));

    /* Printed last and on its own line: this is what gets compared across a
       change, and it must not move. */
    printf("\n%llu regions, decode checksum %016llx\n", (unsigned long long)regions, (unsigned long long)expected);

    free(image);
    return EXIT_SUCCESS;
}
