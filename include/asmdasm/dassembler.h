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
bool disassembly_listing(char *source);

#endif
