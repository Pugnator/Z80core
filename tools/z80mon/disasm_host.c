/**
 * @file   disasm_host.c
 * @brief  What zasm's disassembler needs when linked without the assembler
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
 *
 * This file is part of Z80core, released under the terms of the GNU General
 * Public License version 2. See LICENSE.md for the full text.
 *
 * z80mon disassembles with zasm's own disassembler rather than a second
 * implementation that could disagree with it. dassembler.c reads a handful of
 * globals that assembler.c normally defines; pulling assembler.c in would drag
 * the whole parser along, so the few that matter are defined here instead.
 *
 * run_pass stays at PASS2 for the monitor's purposes: pass 1 is where the
 * disassembler collects jump targets into its label table, and a read-only
 * view of memory has no use for that.
 */

#include "disasm_host.h"

#include <common.h>
#include <dassembler.h>

#include <stdlib.h>
#include <string.h>

RUNPASS run_pass = PASS2;
int current_line = 0;
int verbose = 0;

size_t z80mon_disasm(const uint8_t *code, size_t size, uint16_t pc, char *out, size_t out_size)
{
    if (!code || !out || out_size == 0)
    {
        return 0;
    }
    out[0] = '\0';

    dsmctx ctx;
    memset(&ctx, 0, sizeof ctx);
    /* the disassembler reads through this pointer and never writes to it */
    ctx.prog = (uint8_t *)code;
    ctx.data_size = size;
    ctx.PC = pc;

    dsmopc *decoded = disasm_fetch_next_opcode(&ctx);
    if (!decoded)
    {
        return 0;
    }

    snprintf(out, out_size, "%s", decoded->mnemonic ? decoded->mnemonic : "?");
    free(decoded->mnemonic);
    free(decoded);

    /* PC is 16 bits and wraps at the top of memory, so take the difference there */
    const uint16_t used = (uint16_t)(ctx.PC - pc);
    return used ? used : 1u;
}
