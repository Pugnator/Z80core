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

#ifdef __cplusplus
}
#endif
