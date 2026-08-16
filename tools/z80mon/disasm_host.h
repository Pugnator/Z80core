/**
 * @file   disasm_host.h
 * @brief  One instruction of disassembly, for the monitor
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
 *
 * This file is part of Z80core, released under the terms of the GNU General
 * Public License version 2. See LICENSE.md for the full text.
 *
 * Deliberately narrow. zasm's disassembler headers drag in the assembler's
 * world - the parser's generated header among it - which has no business in a
 * C++ translation unit, so the interface between them is this one function.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Disassemble the instruction at @p pc.
 * @param code  Memory to read, indexed by address.
 * @param size  How much of it there is.
 * @param pc    Where to decode.
 * @param out   Receives the text; always terminated.
 * @return Bytes the instruction occupies, or 0 if nothing could be decoded.
 */
size_t z80mon_disasm(const uint8_t *code, size_t size, uint16_t pc, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
