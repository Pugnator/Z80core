/**
 * @file   zasm.h
 * @brief  Z80 assembler, as an embeddable library
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
 *
 * This file is part of Z80core, released under the terms of the GNU General
 * Public License version 2. See LICENSE.md for the full text.
 *
 * Assembles Z80 source text into bytes. Diagnostics come back through a
 * callback rather than being printed, so a host can put them in a window, a
 * log, or a compiler-style error list.
 *
 * NOT REENTRANT. The parser, the scanner and the assembler keep their state in
 * globals, so one assembly may be in flight at a time, process-wide. Calls are
 * independent of each other - state is reset on entry - but they must not
 * overlap. See docs/EMBEDDING.md.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** Output encodings zasm_assemble() can produce. */
typedef enum
{
    ZASM_FORMAT_BIN = 0, /**< raw bytes, from the lowest address written */
    ZASM_FORMAT_TAP,     /**< ZX Spectrum tape image, with a BASIC loader */
    ZASM_FORMAT_IHEX     /**< Intel HEX records */
} zasm_format;

/** What an assembly produced. Release it with zasm_image_free(). */
typedef struct
{
    uint8_t *bytes;  /**< the output; NULL when nothing was produced */
    size_t size;     /**< how many bytes */
    uint16_t origin; /**< the lowest address the source assembled to */
} zasm_image;

/**
 * @brief Receives one diagnostic.
 * @param user Whatever was handed to zasm_assemble().
 * @param line Source line it refers to, or 0 when it belongs to no line.
 * @param message The text, without a trailing newline.
 */
typedef void (*zasm_diag_fn)(void *user, int line, const char *message);

/**
 * @brief Assemble source text.
 *
 * @param source Null-terminated source. Not modified.
 * @param format What to produce.
 * @param out    Receives the result on success; zeroed on failure.
 * @param diag   Called for every error, or NULL to discard them.
 * @param user   Passed back to @p diag.
 *
 * @return true if the source assembled without errors.
 */
bool zasm_assemble(const char *source, zasm_format format, zasm_image *out, zasm_diag_fn diag, void *user);

/** Release what zasm_assemble() produced. Safe on a zeroed image. */
void zasm_image_free(zasm_image *image);

/** The library's version, as "major.minor.patch". */
const char *zasm_version(void);

#ifdef __cplusplus
}
#endif
