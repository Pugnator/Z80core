/**
 * @file   zdasm.h
 * @brief  Z80 disassembler, as an embeddable library
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
 *
 * This file is part of Z80core, released under the terms of the GNU General
 * Public License version 2. See LICENSE.md for the full text.
 *
 * Decodes Z80 machine code into text. It keeps no state between calls, reads
 * only the buffer it is handed, allocates nothing and prints nothing: what a
 * host does with the result is the host's business.
 *
 * See docs/EMBEDDING.md for how to build this into another project.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** Longest text any one instruction produces, terminator included. */
#define ZDASM_MAX_TEXT 64

/** One decoded instruction. */
typedef struct
{
    uint16_t address;          /**< where it was decoded from */
    uint8_t length;            /**< bytes it occupies, never 0 */
    char text[ZDASM_MAX_TEXT]; /**< the disassembly, always terminated */
    bool known;                /**< false when the bytes are not an instruction */
    bool branches;             /**< true if it transfers control to target */
    uint16_t target;           /**< where to, when branches is true */
    /**
     * Whether control can reach the following instruction. False for an
     * unconditional jump or return, true for everything else including calls
     * and every conditional form. It is what separates "this jumps somewhere"
     * from "and it might come back here", which a linear walk cannot tell.
     */
    bool continues;
} zdasm_insn;

/**
 * @brief Decode the one instruction at @p pc.
 *
 * A byte that starts no known instruction is reported with known = false,
 * length 1 and text rendered as a "defb", so a caller walking a buffer always
 * makes progress and never has to guess an instruction length.
 *
 * @return The number of bytes consumed, or 0 if @p pc is out of range.
 */
uint8_t zdasm_decode(const uint8_t *code, size_t size, uint16_t pc, zdasm_insn *out);

/**
 * @brief Decode one instruction, keeping only its text.
 * @return Bytes consumed, or 0 if @p pc is out of range.
 */
uint8_t zdasm_one(const uint8_t *code, size_t size, uint16_t pc, char *out, size_t out_size);

/**
 * @brief Decode from @p start until @p end, one call per instruction.
 * @param emit Receives each instruction; return false to stop early.
 * @return How many instructions were emitted.
 */
size_t zdasm_range(const uint8_t *code, size_t size, uint16_t start, uint16_t end,
                   bool (*emit)(void *user, const zdasm_insn *insn), void *user);

/* ---------------------------------------------------------------- */
/* Telling code from data                                            */
/* ---------------------------------------------------------------- */

typedef enum
{
    ZDASM_CODE,  /**< reached by following control flow from an entry point */
    ZDASM_DATA,  /**< reached by nothing, and not printable */
    ZDASM_STRING /**< reached by nothing, and reads as text */
} zdasm_region_kind;

typedef struct
{
    uint16_t start;
    uint16_t length;
    zdasm_region_kind kind;
} zdasm_region;

/** Shortest run of printable bytes that will be called a string. */
#define ZDASM_STRING_MINIMUM 6

/**
 * @brief Work out which parts of a buffer are code and which are not.
 *
 * Walks the control-flow graph from the given entry points, marking everything
 * it can reach as code, and classifies what is left. This is deliberately a
 * pass over a buffer rather than something inside zdasm_decode(): that function
 * is stateless, allocation-free and per-instruction, which is what makes it
 * embeddable, and region analysis is a different shape of problem.
 *
 * Reachability first, and strings only over what nothing reaches. Looking for
 * printable runs alone finds mostly code - on a Spectrum ROM the system
 * variables live at 0x5Cxx, so `2A 5D 5C`, an ordinary `LD HL,(5C5D)`, reads as
 * `*]\`. Classifying only unreachable bytes removes that whole class of false
 * positive by construction rather than by hoping a threshold hides it.
 *
 * What this cannot see, stated so the limit is chosen rather than discovered:
 * `JP (HL)`, jump tables, self-modifying code, and anything reached only
 * through a computed address. Supply extra entry points for those.
 *
 * @param entries Addresses execution can begin at; 0 alone is a fair default
 *                for a ROM, plus the RST vectors and 0x66 for a Spectrum.
 * @param out     Filled with regions covering the whole buffer in address
 *                order, with no gaps and no overlaps. May be NULL to count.
 * @return Regions the buffer needs. Greater than @p out_capacity means the
 *         answer was truncated, not that it failed.
 */
size_t zdasm_analyse(const uint8_t *code, size_t size, const uint16_t *entries, size_t entry_count, zdasm_region *out,
                     size_t out_capacity);

#ifdef __cplusplus
}
#endif
