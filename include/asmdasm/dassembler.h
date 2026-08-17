/**
 * @file   dassembler.h
 * @brief  The disassembly listing zasm -d prints
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
 *
 * This file is part of Z80core, released under the terms of the GNU General
 * Public License version 2. See LICENSE.md for the full text.
 *
 * For decoding instructions in your own program, use zdasm.h instead: it is
 * the disassembler proper, and it has no opinion about files or printing.
 */

#ifndef DISASM_H
#define DISASM_H

#include <common.h>

/**
 * @brief Print a disassembly of @p source to stdout.
 * @return false if the file could not be read.
 */
/**
 * @brief Print a disassembly listing of a binary image.
 *
 * @param analyse When true, separate code from data first and render what
 *                nothing reaches as `defm` and `defb` rather than as
 *                instructions. Off by default because a linear listing is what
 *                a reader usually wants of a known-code image, and because it
 *                changes what the output reassembles from.
 */
bool disassembly_listing(char *source, bool analyse);

#endif
