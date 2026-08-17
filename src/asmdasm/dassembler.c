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

/**
 * Render a byte inside a single-quoted string the way the assembler reads one
 * back. The quote and the backslash need escaping for the lexer; tab, newline
 * and carriage return get their named escapes so a directive never spans
 * physical lines - a raw newline inside quotes survives the scanner but not
 * text-mode pipes, universal-newline reads or anything else that edits line
 * endings. The double quote must be escaped too: the lexer rewrites the
 * delimiters to double quotes, and deft() then skips every bare one it sees,
 * so an unescaped '"' in the middle would silently vanish from the output.
 *
 * @return How many characters were printed, so the caller can keep a
 *         directive inside the lexer's token limit.
 */
static unsigned print_string_byte(uint8_t byte)
{
    switch (byte)
    {
    case '\'':
        fputs("\\'", stdout);
        return 2u;
    case '"':
        fputs("\\\"", stdout);
        return 2u;
    case '\\':
        fputs("\\\\", stdout);
        return 2u;
    case '\t':
        fputs("\\t", stdout);
        return 2u;
    case '\n':
        fputs("\\n", stdout);
        return 2u;
    case '\r':
        fputs("\\r", stdout);
        return 2u;
    default:
        putchar((int)byte);
        return 1u;
    }
}

/**
 * The most escaped characters one defm puts between its quotes. The scanner
 * copies the whole token - both quotes included - into a MAX_TOKEN_SIZE
 * buffer and reports anything longer rather than truncating it, so a string
 * region longer than this is emitted as several directives. 48 leaves margin
 * under the real limit and keeps the lines readable.
 */
#define STRING_CHUNK 48u

/**
 * A string region, as source.
 *
 * The last byte gets a term of its own when bit 7 is set. Marking the end of a
 * string that way is the usual Z80 convention and the Spectrum ROM's, and such
 * a byte is not text - so it cannot sit inside the quotes and come back out as
 * the same byte. defm takes a list, so it rides along in the same directive
 * rather than needing a defb after it.
 */
static void print_string_region(const uint8_t *image, const zdasm_region *region)
{
    uint16_t length = region->length;
    const bool terminated = length > 0u && 0u != (image[region->start + length - 1u] & 0x80u);
    if (terminated)
    {
        --length;
    }

    uint16_t i = 0;
    do
    {
        const uint16_t line_start = (uint16_t)(region->start + i);
        unsigned used = 0;

        printf("\tdefm '");
        while (i < length && used < STRING_CHUNK)
        {
            used += print_string_byte(image[region->start + i]);
            ++i;
        }
        putchar('\'');

        if (terminated && i == length)
        {
            printf(", %#.2x", image[region->start + region->length - 1u]);
        }
        printf("\t\t\t;%.4Xh\n", line_start);
    } while (i < length);
}

/** A data region, as defb lines of eight bytes. */
static void print_data_region(const uint8_t *image, const zdasm_region *region)
{
    for (uint16_t i = 0; i < region->length; i += 8u)
    {
        const uint16_t here = (uint16_t)(region->start + i);
        const uint16_t left = (uint16_t)(region->length - i);
        const uint16_t run = (left < 8u) ? left : 8u;

        printf("\tdefb ");
        for (uint16_t j = 0; j < run; ++j)
        {
            printf("%s%#.2x", j ? ", " : "", image[here + j]);
        }
        printf("\t\t\t;%.4Xh\n", here);
    }
}

bool disassembly_listing(char *source, bool analyse)
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

    /*
     * Which stretches get decoded as instructions. Without analysis, all of
     * them - the linear sweep this has always done. With it, only what control
     * flow can reach from the start of the image.
     */
    zdasm_region *regions = NULL;
    size_t region_count = 0;

    if (analyse)
    {
        const uint16_t entry = 0;
        region_count = zdasm_analyse(image, (size_t)size, &entry, 1u, NULL, 0);
        if (region_count > 0)
        {
            regions = malloc(region_count * sizeof *regions);
            if (!regions)
            {
                puts("Memory allocation failed");
                free(image);
                return false;
            }
            region_count = zdasm_analyse(image, (size_t)size, &entry, 1u, regions, region_count);
        }
    }

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
    size_t region = 0;
    for (uint32_t pc = 0; pc < (uint32_t)size;)
    {
        while (regions && region < region_count && pc >= (uint32_t)regions[region].start + regions[region].length)
        {
            ++region;
        }

        if (regions && region < region_count && ZDASM_CODE != regions[region].kind &&
            pc == (uint32_t)regions[region].start)
        {
            if (has_label(labels, (uint16_t)pc))
            {
                printf("L%.4X:\n", (unsigned)pc);
            }
            if (ZDASM_STRING == regions[region].kind)
            {
                print_string_region(image, &regions[region]);
            }
            else
            {
                print_data_region(image, &regions[region]);
            }
            pc += regions[region].length;
            continue;
        }

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

    free(regions);
    free_labels(&labels);
    free(image);
    return true;
}
