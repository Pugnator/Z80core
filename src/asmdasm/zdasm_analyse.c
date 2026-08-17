/**
 * @file   zdasm_analyse.c
 * @brief  Which bytes of a buffer are code, and which are something else
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
 *
 * This file is part of Z80core, released under the terms of the GNU General
 * Public License version 2. See LICENSE.md for the full text.
 *
 * Deliberately beside zdasm_decode() rather than inside it. That function is
 * stateless, allocation-free and per-instruction, which is what makes it
 * embeddable; this is a pass over a whole buffer that needs a work list and a
 * byte map, and mixing the two would cost the first its character.
 *
 * Reachability comes first and strings second, over what nothing reaches. The
 * order is the whole design: printable runs alone find mostly code, because on
 * a Spectrum ROM the system variables live at 0x5Cxx and `2A 5D 5C` - an
 * everyday LD HL,(5C5D) - reads as `*]\`. Classifying only unreachable bytes
 * removes that class of mistake by construction instead of hoping a threshold
 * hides it.
 */

#include "zdasm.h"

#include <stdlib.h>
#include <string.h>

/* One byte of the map. Kept as bytes rather than bits: the buffer is at most
   64K, and a bitmap would save 56K at the cost of every access being fiddly. */
#define MARK_UNSEEN 0u
#define MARK_CODE 1u

/**
 * The work list. Addresses waiting to be traced from, without recursion - a
 * deeply nested call graph would otherwise decide how much stack this needs.
 */
typedef struct
{
    uint16_t *items;
    size_t count;
    size_t capacity;
} worklist;

static bool worklist_push(worklist *list, uint16_t address)
{
    if (list->count == list->capacity)
    {
        const size_t grown = list->capacity ? list->capacity * 2u : 64u;
        uint16_t *items = (uint16_t *)realloc(list->items, grown * sizeof *items);
        if (!items)
        {
            return false;
        }
        list->items = items;
        list->capacity = grown;
    }
    list->items[list->count++] = address;
    return true;
}

/**
 * Follow control flow from every entry point, marking what it reaches.
 *
 * A trace runs forward until it meets something that does not continue - an
 * unconditional jump or a return - or reaches a byte already seen. Branch
 * targets go on the list rather than being followed immediately, so the depth
 * of the call graph costs nothing.
 */
static void trace(const uint8_t *code, size_t size, const uint16_t *entries, size_t entry_count, uint8_t *map)
{
    worklist list = {NULL, 0u, 0u};

    for (size_t i = 0; i < entry_count; ++i)
    {
        if (entries[i] < size && !worklist_push(&list, entries[i]))
        {
            break;
        }
    }

    while (list.count > 0)
    {
        uint16_t pc = list.items[--list.count];

        while (pc < size && MARK_UNSEEN == map[pc])
        {
            zdasm_insn insn;
            const uint8_t length = zdasm_decode(code, size, pc, &insn);
            if (0u == length)
            {
                break;
            }

            /* A byte that decodes to nothing is not code, and following what
               comes after it would be following a guess. */
            if (!insn.known)
            {
                break;
            }

            for (uint8_t i = 0; i < length && (size_t)(pc + i) < size; ++i)
            {
                map[pc + i] = MARK_CODE;
            }

            if (insn.branches && insn.target < size && MARK_UNSEEN == map[insn.target])
            {
                if (!worklist_push(&list, insn.target))
                {
                    break;
                }
            }

            if (!insn.continues)
            {
                break;
            }
            pc = (uint16_t)(pc + length);
        }
    }

    free(list.items);
}

static bool printable(uint8_t byte)
{
    return (byte >= 0x20u && byte < 0x7Fu) || '\t' == byte || '\n' == byte || '\r' == byte;
}

/**
 * Is the unreachable run starting here long enough, and printable enough, to
 * call text?
 *
 * A trailing byte with bit 7 set counts as part of it. Marking the end of a
 * string that way is the usual Z80 convention and the Spectrum ROM's - it is
 * why the messages in it look truncated to anything that only accepts ASCII -
 * and finding one is evidence rather than a guess, so a run that ends in one
 * is accepted a byte shorter than it would otherwise need to be.
 */
static size_t string_run(const uint8_t *code, size_t size, size_t start, size_t limit)
{
    size_t length = 0;
    while (start + length < limit && start + length < size && printable(code[start + length]))
    {
        ++length;
    }

    if (0u == length)
    {
        return 0u;
    }

    const bool terminated = (start + length < limit) && (start + length < size) &&
                            (0u != (code[start + length] & 0x80u)) &&
                            printable((uint8_t)(code[start + length] & 0x7Fu));

    const size_t minimum = terminated ? (ZDASM_STRING_MINIMUM - 1u) : ZDASM_STRING_MINIMUM;
    if (length < minimum)
    {
        return 0u;
    }

    return terminated ? length + 1u : length;
}

/**
 * Where the answer is accumulated. The last region is tracked here rather than
 * read back out of the caller's array, so merging keeps working once the array
 * is full - otherwise a truncated run would count adjacent like regions
 * separately and report a number larger than the answer really needs.
 */
typedef struct
{
    zdasm_region *out;
    size_t capacity;
    size_t count;
    zdasm_region last;
    bool have_last;
} region_builder;

static void flush(region_builder *builder)
{
    if (!builder->have_last)
    {
        return;
    }
    /* Counting continues past the capacity, so the caller learns how much room
       the answer needed rather than being handed a silently short list. */
    if (builder->out && builder->count < builder->capacity)
    {
        builder->out[builder->count] = builder->last;
    }
    ++builder->count;
    builder->have_last = false;
}

/** Append a region, merging it into the previous one when they agree. */
static void append(region_builder *builder, uint16_t start, uint16_t length, zdasm_region_kind kind)
{
    /*
     * Strings never merge with each other. Two runs are adjacent only because
     * the first one ended, and what ended it was a byte with bit 7 set - so
     * merging them would bury that terminator in the middle of a region whose
     * interior is supposed to be printable, and it would come back out as a
     * raw high byte inside quotes.
     */
    const bool mergeable = (ZDASM_STRING != kind);

    if (mergeable && builder->have_last && builder->last.kind == kind &&
        builder->last.start + builder->last.length == start)
    {
        builder->last.length = (uint16_t)(builder->last.length + length);
        return;
    }

    flush(builder);
    builder->last.start = start;
    builder->last.length = length;
    builder->last.kind = kind;
    builder->have_last = true;
}

size_t zdasm_analyse(const uint8_t *code, size_t size, const uint16_t *entries, size_t entry_count, zdasm_region *out,
                     size_t out_capacity)
{
    if (!code || 0u == size)
    {
        return 0u;
    }
    if (size > 0x10000u)
    {
        size = 0x10000u;
    }

    uint8_t *map = (uint8_t *)calloc(size, 1u);
    if (!map)
    {
        return 0u;
    }

    if (entries && entry_count > 0u)
    {
        trace(code, size, entries, entry_count, map);
    }

    region_builder builder;
    memset(&builder, 0, sizeof builder);
    builder.out = out;
    builder.capacity = out_capacity;

    size_t at = 0u;

    while (at < size)
    {
        if (MARK_CODE == map[at])
        {
            size_t length = 0u;
            while (at + length < size && MARK_CODE == map[at + length])
            {
                ++length;
            }
            append(&builder, (uint16_t)at, (uint16_t)length, ZDASM_CODE);
            at += length;
            continue;
        }

        /* how far this unreachable stretch runs, which is as far as a string
           inside it may reach */
        size_t limit = at;
        while (limit < size && MARK_CODE != map[limit])
        {
            ++limit;
        }

        const size_t text = string_run(code, size, at, limit);
        if (text > 0u)
        {
            append(&builder, (uint16_t)at, (uint16_t)text, ZDASM_STRING);
            at += text;
            continue;
        }

        /* not text here: one byte of data, then look again - so a string that
           begins partway through a stretch is still found */
        append(&builder, (uint16_t)at, 1u, ZDASM_DATA);
        ++at;
    }

    flush(&builder);
    free(map);
    return builder.count;
}
