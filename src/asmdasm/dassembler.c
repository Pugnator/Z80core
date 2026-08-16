/**
 * @file   dassembler.c
 * @brief  The disassembly listing zasm -d prints
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
 *
 * This file is part of Z80core, released under the terms of the GNU General
 * Public License version 2. See LICENSE.md for the full text.
 *
 * The decoding itself is zdasm.c, which knows nothing about files or output.
 * What is left here is what a command line wants on top: read the image, walk
 * it once to find the branch targets, then again to print, with a label on
 * every line something jumps to.
 */

#include "dassembler.h"

#include "uthash.h"
#include "zdasm.h"

/** An address something branches to, so pass 2 can put a label on it. */
typedef struct
{
    uint16_t address;
    UT_hash_handle hh;
} disasm_label;

static void note_label(disasm_label **labels, uint16_t address)
{
    disasm_label *found = NULL;
    HASH_FIND(hh, *labels, &address, sizeof(address), found);
    if (found)
    {
        return;
    }

    found = malloc(sizeof *found);
    if (!found)
    {
        return;
    }
    found->address = address;
    HASH_ADD(hh, *labels, address, sizeof(address), found);
}

static bool has_label(disasm_label *labels, uint16_t address)
{
    disasm_label *found = NULL;
    HASH_FIND(hh, labels, &address, sizeof(address), found);
    return NULL != found;
}

static void free_labels(disasm_label **labels)
{
    disasm_label *cur, *tmp;
    HASH_ITER(hh, *labels, cur, tmp)
    {
        HASH_DEL(*labels, cur);
        free(cur);
    }
    *labels = NULL;
}

bool disassembly_listing(char *source)
{
    /* binary input: text mode mangles CRLF pairs and stops at 0x1A */
    FILE *in = fopen(source, "rb");
    if (!in)
    {
        puts("Failed to open source file");
        return false;
    }

    if (0 != fseek(in, 0, SEEK_END))
    {
        puts("Failed to read input file");
        fclose(in);
        return false;
    }
    const long size = ftell(in);
    rewind(in);
    if (size < 0)
    {
        puts("Failed to read input file");
        fclose(in);
        return false;
    }
    if (size > PROG_SIZE)
    {
        puts("Input does not fit into the 64K address space");
        fclose(in);
        return false;
    }

    uint8_t *image = malloc((size_t)size ? (size_t)size : 1);
    if (!image)
    {
        puts("Memory allocation failed");
        fclose(in);
        return false;
    }

    if (fread(image, 1, (size_t)size, in) != (size_t)size)
    {
        puts("Failed to read input file");
        free(image);
        fclose(in);
        return false;
    }
    fclose(in);

    printf("; %s, %ld bytes\n", source, size);

    /* pass 1: where does anything branch to */
    disasm_label *labels = NULL;
    for (uint32_t pc = 0; pc < (uint32_t)size;)
    {
        zdasm_insn insn;
        const uint8_t used = zdasm_decode(image, (size_t)size, (uint16_t)pc, &insn);
        if (!used)
        {
            break;
        }
        if (insn.branches)
        {
            note_label(&labels, insn.target);
        }
        pc += used;
    }

    /* pass 2: print, labelling the lines pass 1 marked */
    for (uint32_t pc = 0; pc < (uint32_t)size;)
    {
        zdasm_insn insn;
        const uint8_t used = zdasm_decode(image, (size_t)size, (uint16_t)pc, &insn);
        if (!used)
        {
            break;
        }
        if (has_label(labels, insn.address))
        {
            printf("L%.4X:\n", insn.address);
        }
        printf("\t%s\t\t\t;%.4Xh\n", insn.text, insn.address);
        pc += used;
    }

    free_labels(&labels);
    free(image);
    return true;
}
