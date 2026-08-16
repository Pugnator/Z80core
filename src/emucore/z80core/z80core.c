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
 * PHASE 0: the engine below is the one the finished core uses - a step cursor
 * walking a table of per-edge step functions. What is missing is the
 * instruction set: instead of decoding, the cursor replays one M1 fetch
 * forever. That is enough to exercise the interface end to end and to measure
 * what an edge costs, which is what phase 0 is for.
 */

#include "z80core.h"

#include <stdlib.h>
#include <string.h>

#define Z80CORE_VERSION "0.0.1-phase0"

/** Outputs the core drives; the rest of ctrl belongs to the host. */
#define Z80_OUTPUT_PINS (Z80_M1 | Z80_MREQ | Z80_IORQ | Z80_RD | Z80_WR | Z80_RFSH | Z80_HALT | Z80_BUSAK)

struct z80_t
{
    /* register file: only what the skeleton touches, the rest lands in phase 1 */
    uint16_t PC;
    uint8_t I;
    uint8_t R;

    /* engine */
    uint8_t step;   /**< cursor into the current step list */
    uint8_t clk;    /**< the level the core last advanced to */
    uint64_t edges; /**< edges advanced since construction or reset */
};

/**
 * A step runs on one clock edge and may drive pins. Steps never read the
 * cursor or advance it; the engine owns that.
 */
typedef void (*z80_step_fn)(z80_t *cpu, z80_pins_t *pins);

static void step_m1_t1_rise(z80_t *cpu, z80_pins_t *pins)
{
    pins->A = cpu->PC;
    pins->ctrl |= Z80_M1;
}

static void step_m1_t1_fall(z80_t *cpu, z80_pins_t *pins)
{
    (void)cpu;
    pins->ctrl |= Z80_MREQ | Z80_RD;
}

static void step_idle(z80_t *cpu, z80_pins_t *pins)
{
    (void)cpu;
    (void)pins;
}

static void step_m1_t3_rise(z80_t *cpu, z80_pins_t *pins)
{
    /* the real core latches pins->D here; the skeleton only releases the bus */
    cpu->PC = (uint16_t)(cpu->PC + 1u);
    pins->ctrl &= ~(uint32_t)(Z80_M1 | Z80_MREQ | Z80_RD);
}

static void step_m1_t3_fall(z80_t *cpu, z80_pins_t *pins)
{
    cpu->R = (uint8_t)((cpu->R & 0x80u) | ((cpu->R + 1u) & 0x7Fu));
    pins->A = (uint16_t)(((uint16_t)cpu->I << 8) | cpu->R);
    pins->ctrl |= Z80_RFSH;
}

static void step_m1_t4_fall(z80_t *cpu, z80_pins_t *pins)
{
    (void)cpu;
    pins->ctrl &= ~(uint32_t)Z80_RFSH;
}

/**
 * One M1 opcode fetch, edge by edge (spec section 5.3). Phase 0 loops on this
 * list; phase 1 selects a list per instruction from the opcode table.
 */
static const z80_step_fn m1_fetch[] = {
    step_m1_t1_rise, /* T1 rise: address out, M1 asserted   */
    step_m1_t1_fall, /* T1 fall: MREQ, RD asserted          */
    step_idle,       /* T2 rise                             */
    step_idle,       /* T2 fall: WAIT sampled here          */
    step_m1_t3_rise, /* T3 rise: opcode latched, bus freed  */
    step_m1_t3_fall, /* T3 fall: refresh address, RFSH      */
    step_idle,       /* T4 rise: decode                     */
    step_m1_t4_fall  /* T4 fall: RFSH released              */
};

#define M1_FETCH_EDGES ((uint8_t)(sizeof m1_fetch / sizeof m1_fetch[0]))

const char *z80_version(void)
{
    return Z80CORE_VERSION;
}

z80_t *z80_new(void)
{
    z80_t *cpu = calloc(1, sizeof *cpu);
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
    /* spec 7.1: PC and the interrupt state clear; SP and AF land in phase 1 */
    cpu->PC = 0;
}

/**
 * @brief Advance one edge and report what moved.
 *
 * The mask is computed by comparing the pins before and after the step rather
 * than by each step announcing itself: a step that writes the value already
 * present has changed nothing, and the host should not be told otherwise.
 */
static uint32_t advance(z80_t *cpu, z80_pins_t *pins)
{
    const uint16_t address_before = pins->A;
    const uint8_t data_before = pins->D;
    const uint32_t ctrl_before = pins->ctrl;

    m1_fetch[cpu->step](cpu, pins);

    cpu->step = (uint8_t)((cpu->step + 1u) % M1_FETCH_EDGES);
    ++cpu->edges;

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
