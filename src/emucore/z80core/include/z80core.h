/**
 * @file   z80core.h
 * @brief  Public interface of the Z80 CPU core
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
 *
 * This file is part of Z80core, released under the terms of the GNU General
 * Public License version 2. See LICENSE.md for the full text.
 *
 * The core is the CPU and nothing else: no memory, no devices, no scheduler.
 * A host advances it one clock edge at a time and answers its bus requests
 * through the pin structure. See docs/CPU-CORE-SPEC.md.
 *
 * PHASE 0: this is a walking skeleton. The step engine, the pin handling and
 * the API are real; the instruction set is not here yet, and the steps below
 * replay a fixed M1 fetch so the interface can be exercised and measured.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#if defined(_WIN32) && defined(Z80CORE_SHARED)
#if defined(Z80CORE_BUILD)
#define Z80_API __declspec(dllexport)
#else
#define Z80_API __declspec(dllimport)
#endif
#else
#define Z80_API
#endif

/**
 * Control pins. Every signal is named without its overbar and is true when
 * asserted, which is the opposite of the electrical level on the real part.
 */
enum z80_ctrl_pin
{
    Z80_M1 = 1u << 0,    /**< opcode fetch cycle */
    Z80_MREQ = 1u << 1,  /**< memory request */
    Z80_IORQ = 1u << 2,  /**< I/O request; with M1, interrupt acknowledge */
    Z80_RD = 1u << 3,    /**< read strobe */
    Z80_WR = 1u << 4,    /**< write strobe */
    Z80_RFSH = 1u << 5,  /**< refresh address on A0..A6 */
    Z80_HALT = 1u << 6,  /**< CPU is halted */
    Z80_BUSAK = 1u << 7, /**< bus acknowledged, buses floating */

    Z80_WAIT = 1u << 8,   /**< in: stretch the current cycle */
    Z80_INT = 1u << 9,    /**< in: maskable interrupt request */
    Z80_NMI = 1u << 10,   /**< in: non-maskable interrupt, edge triggered */
    Z80_RESET = 1u << 11, /**< in: reset request */
    Z80_BUSRQ = 1u << 12  /**< in: bus request */
};

/** Extra bits used only in the mask returned by z80_tick(). */
enum z80_changed_bit
{
    Z80_CHANGED_A = 1u << 30, /**< the address bus was driven to a new value */
    Z80_CHANGED_D = 1u << 31  /**< the data bus was driven to a new value */
};

/**
 * The pins, owned by the host. Write the inputs, call z80_tick(), read the
 * outputs. Fixed-width fields only, so 32- and 64-bit builds agree on the
 * layout.
 */
typedef struct
{
    uint16_t A;    /**< address bus */
    uint8_t D;     /**< data bus, bidirectional */
    uint32_t ctrl; /**< control pins, z80_ctrl_pin bits */
} z80_pins_t;

typedef struct z80_t z80_t;

Z80_API const char *z80_version(void);

Z80_API z80_t *z80_new(void);
Z80_API void z80_free(z80_t *cpu);
Z80_API void z80_reset(z80_t *cpu);

/**
 * @brief Advance to the next clock edge.
 * @param clk The new clock level, 0 or 1. Passing the level the core is
 *            already at does nothing, which is how a stopped, gated or
 *            hand-stepped clock behaves on real hardware.
 * @return Mask of output pins driven to a new value on this edge, 0 if none.
 *
 * After z80_new() and z80_reset() the clock is **low**, so the first call that
 * advances anything is z80_tick(cpu, pins, 1). A driver that starts at 0
 * silently performs one fewer edge than it thinks.
 */
Z80_API uint32_t z80_tick(z80_t *cpu, z80_pins_t *pins, int clk);

/**
 * @brief Run @p edges clock edges without returning to the caller.
 *
 * Same work as calling z80_tick() in a loop with an alternating level, but
 * crossing the host boundary once instead of once per edge. Comparing the two
 * is what tells us what the boundary costs (docs/CPU-CORE-SPEC.md section 10.4).
 *
 * @return The number of edges advanced.
 */
Z80_API uint64_t z80_run(z80_t *cpu, z80_pins_t *pins, uint64_t edges);

/** Total edges advanced since construction or reset. */
Z80_API uint64_t z80_edges(const z80_t *cpu);
