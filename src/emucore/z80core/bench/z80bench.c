/**
 * @file   z80bench.c
 * @brief  Measures what one clock edge costs, with no host in the way
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
 *
 * This file is part of Z80core, released under the terms of the GNU General
 * Public License version 2. See LICENSE.md for the full text.
 *
 * This is the "core alone" figure of docs/CPU-CORE-SPEC.md section 10.4. It is
 * the floor: no Lua, no simulator, nothing but the engine. Compare it against
 * the same core driven from a Lua script, and again from Proteus, and the
 * differences are what the boundaries cost.
 */

#include "z80core.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/** Real time for a 3.5 MHz Z80 is two edges per clock. */
#define REALTIME_EDGES_PER_SECOND 7000000.0

static double seconds_since(clock_t start)
{
    return (double)(clock() - start) / (double)CLOCKS_PER_SEC;
}

static void report(const char *what, uint64_t edges, double seconds)
{
    const double rate = seconds > 0.0 ? (double)edges / seconds : 0.0;
    printf("%-28s %10.2f M edges/s   %6.2fx real time   (%.3f s)\n", what, rate / 1e6, rate / REALTIME_EDGES_PER_SECOND,
           seconds);
}

int main(int argc, char **argv)
{
    uint64_t edges = 20000000;
    if (argc > 1)
    {
        const long long requested = atoll(argv[1]);
        if (requested > 0)
        {
            edges = (uint64_t)requested;
        }
    }

    z80_t *cpu = z80_new();
    if (!cpu)
    {
        fprintf(stderr, "z80bench: out of memory\n");
        return EXIT_FAILURE;
    }

    printf("z80core %s, %llu edges per measurement\n\n", z80_version(), (unsigned long long)edges);

    z80_pins_t pins = {0};

    /* one call per edge: what a host driving the clock actually does. The
       clock starts low, so the first edge asked for is a rising one - starting
       at 0 would make the first call a no-op and quietly measure one edge
       fewer than reported. */
    clock_t start = clock();
    for (uint64_t i = 0; i < edges; ++i)
    {
        (void)z80_tick(cpu, &pins, (int)((i + 1u) & 1u));
    }
    report("z80_tick() per edge", edges, seconds_since(start));

    if (z80_edges(cpu) != edges)
    {
        fprintf(stderr, "z80bench: stepped %llu edges but the core counted %llu\n", (unsigned long long)edges,
                (unsigned long long)z80_edges(cpu));
        z80_free(cpu);
        return EXIT_FAILURE;
    }

    /* the same work with one call: the engine without any call overhead */
    z80_reset(cpu);
    pins = (z80_pins_t){0};
    start = clock();
    (void)z80_run(cpu, &pins, edges);
    report("z80_run() batched", edges, seconds_since(start));

    if (z80_edges(cpu) != edges)
    {
        fprintf(stderr, "z80bench: edge counter disagrees: %llu != %llu\n", (unsigned long long)z80_edges(cpu),
                (unsigned long long)edges);
        z80_free(cpu);
        return EXIT_FAILURE;
    }

    z80_free(cpu);
    return EXIT_SUCCESS;
}
