/**
 * @file   zex_test.c
 * @brief  Run Frank Cringle's exerciser against the core
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
 *
 * This file is part of Z80core, released under the terms of the GNU General
 * Public License version 2. See LICENSE.md for the full text.
 *
 * FUSE and SingleStepTests each test one instruction at a time from a
 * synthesised machine state. That is their strength - it is how they check bus
 * activity per T-state - and it is also a blind spot: neither can catch a fault
 * that only appears when instructions interact.
 *
 * This one is the opposite shape. It is a real CP/M program that runs tens of
 * millions of instruction sequences and checks each block by CRC. It says
 * nothing whatever about timing, and it is the only thing here that runs a
 * program rather than a case.
 *
 * The CP/M it needs is two calls, and they live here rather than in the core -
 * which still knows nothing about memory, files or a console.
 */

#include "z80core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CPM_WARM_BOOT 0x0000u
#define CPM_BDOS 0x0005u
#define CPM_LOAD 0x0100u

static uint8_t memory[0x10000];

/* what the program printed, kept so the verdict can be read out of it */
static char output[1 << 16];
static size_t output_length;

static void emit(char c)
{
    if (output_length + 1u < sizeof output)
    {
        output[output_length++] = c;
    }
    fputc(c, stdout);
}

/**
 * The whole of CP/M, as far as this program is concerned: print a character,
 * and print a string. Anything else would be a program we are not running.
 */
static void bdos_call(z80_t *cpu)
{
    const uint16_t bc = z80_get(cpu, Z80_REG_BC);
    const uint16_t de = z80_get(cpu, Z80_REG_DE);

    switch (bc & 0xFFu)
    {
    case 2: /* console out, character in E */
        emit((char)(de & 0xFFu));
        break;

    case 9: /* console out, string at DE, terminated by '$' */
        for (uint16_t address = de; '$' != memory[address]; ++address)
        {
            emit((char)memory[address]);
        }
        break;

    default:
        break;
    }
}

static bool load(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file)
    {
        fprintf(stderr, "zex: cannot open %s\n", path);
        return false;
    }
    const size_t read = fread(&memory[CPM_LOAD], 1u, sizeof memory - CPM_LOAD, file);
    fclose(file);

    if (0u == read)
    {
        fprintf(stderr, "zex: %s is empty\n", path);
        return false;
    }

    /* A RET at each entry point, so the call returns once we have serviced it
       by watching for the fetch. The core needs no hook for this: an M1 cycle
       at a known address is the hook. */
    memory[CPM_WARM_BOOT] = 0xC9;
    memory[CPM_BDOS] = 0xC9;
    return true;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: %s program.com [max-megaedges]\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* This runs for minutes. Line buffering means you can watch it work
       instead of staring at an empty file until it finishes. */
    setvbuf(stdout, NULL, _IOLBF, 0);

    memset(memory, 0, sizeof memory);
    if (!load(argv[1]))
    {
        return EXIT_FAILURE;
    }

    /*
     * No practical limit by default. An edge-stepped core dispatches twice per
     * T-state, so this is an order of magnitude slower than an emulator that
     * works an instruction at a time - which is the price of the timing this
     * core exists to have, not a fault to tune around. Expect tens of minutes.
     */
    const uint64_t edge_limit = (argc > 2) ? (uint64_t)strtoull(argv[2], NULL, 10) * 1000000u : UINT64_MAX;

    z80_t *cpu = z80_new();
    z80_reset(cpu);
    z80_set(cpu, Z80_REG_PC, CPM_LOAD);
    z80_set(cpu, Z80_REG_SP, 0xF000);

    z80_pins_t pins = {0};
    int level = 0;
    bool previous_m1 = false;
    bool finished = false;
    uint64_t edges = 0;

    while (!finished && edges < edge_limit)
    {
        level ^= 1;
        (void)z80_tick(cpu, &pins, level);
        ++edges;

        if (pins.ctrl & Z80_MREQ)
        {
            if (pins.ctrl & Z80_RD)
            {
                pins.D = memory[pins.A];
            }
            else if (pins.ctrl & Z80_WR)
            {
                memory[pins.A] = pins.D;
            }
        }

        const bool m1 = 0 != (pins.ctrl & Z80_M1);
        if (m1 && !previous_m1)
        {
            if (CPM_BDOS == pins.A)
            {
                bdos_call(cpu);
            }
            else if (CPM_WARM_BOOT == pins.A)
            {
                finished = true; /* the program returned to CP/M */
            }
        }
        previous_m1 = m1;
    }

    z80_free(cpu);
    output[output_length] = '\0';
    printf("\n");

    if (!finished)
    {
        printf("zex: gave up after %llu edges without reaching the warm boot\n", (unsigned long long)edges);
        return EXIT_FAILURE;
    }

    /*
     * The program reports its own verdict. A CRC mismatch prints "ERROR", and
     * a run that ends without the closing message did not test what it claims
     * to have tested - both are failures, and the second is the one that would
     * otherwise pass quietly.
     */
    const bool saw_error = (NULL != strstr(output, "ERROR"));
    const bool completed = (NULL != strstr(output, "Tests complete"));

    printf("zex: %llu edges, %s\n", (unsigned long long)edges,
           (saw_error || !completed) ? "FAILED" : "every CRC matched");

    if (saw_error)
    {
        printf("zex: at least one CRC did not match\n");
        return EXIT_FAILURE;
    }
    if (!completed)
    {
        printf("zex: the exerciser never printed its closing message\n");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
