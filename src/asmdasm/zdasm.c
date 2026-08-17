/**
 * @file   zdasm.c
 * @brief  Z80 instruction decoding, free of any host
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
 *
 * This file is part of Z80core, released under the terms of the GNU General
 * Public License version 2. See LICENSE.md for the full text.
 *
 * This is the whole disassembler. It depends on the opcode table and the C
 * library and nothing else - no globals, no allocation, no output - so it can
 * be linked into anything. dassembler.c adds the listing, the label table and
 * the two passes that the zasm command line wants on top of it.
 */

#include "zdasm.h"

#include "z80tab.h"

#include <stdio.h>
#include <string.h>

/** LD (IX+d),n and LD (IY+d),n carry a displacement *and* an immediate. */
/**
 * Can control reach the instruction after this one?
 *
 * Only an unconditional jump or return says no. Everything else continues,
 * calls and every conditional form included - a conditional that is not taken
 * falls through, and a call is expected back. HALT continues too: it resumes
 * at the following instruction once an interrupt has been and gone.
 *
 * This is what a linear walk cannot work out for itself, and what reachability
 * analysis needs above all else.
 */
static bool control_continues(uint32_t opcode)
{
    switch (opcode)
    {
    case 0xC3:   /* JP nn      */
    case 0x18:   /* JR e       */
    case 0xC9:   /* RET        */
    case 0xE9:   /* JP (HL)    */
    case 0xDDE9: /* JP (IX)    */
    case 0xFDE9: /* JP (IY)    */
    case 0xED45: /* RETN       */
    case 0xED4D: /* RETI       */
    case 0xED55:
    case 0xED5D:
    case 0xED65:
    case 0xED6D:
    case 0xED75:
    case 0xED7D: /* the undocumented RETN encodings */
        return false;
    default:
        return true;
    }
}

static bool is_double_argumented(uint32_t opcode)
{
    return 0xDD36 == opcode || 0xFD36 == opcode;
}

/** JP nn and its conditional forms. */
static bool is_absolute_jump(uint32_t opcode)
{
    switch (opcode)
    {
    case 0xC3:
    case 0xC2:
    case 0xCA:
    case 0xD2:
    case 0xDA:
    case 0xE2:
    case 0xEA:
    case 0xF2:
    case 0xFA:
        return true;
    default:
        return false;
    }
}

static bool is_call(uint32_t opcode)
{
    switch (opcode)
    {
    case 0xCD:
    case 0xC4:
    case 0xCC:
    case 0xD4:
    case 0xDC:
    case 0xE4:
    case 0xEC:
    case 0xF4:
    case 0xFC:
        return true;
    default:
        return false;
    }
}

static const opcode_table *find_opcode(uint32_t instruction)
{
    for (int i = 0; opcode_tab[i].mnemo; i++)
    {
        if (instruction == opcode_tab[i].opcode)
        {
            return &opcode_tab[i];
        }
    }
    return NULL;
}

/** An undecodable byte, rendered so a listing stays complete. */
static uint8_t emit_defb(zdasm_insn *out, uint16_t pc, uint8_t byte)
{
    out->address = pc;
    out->length = 1;
    out->known = false;
    out->branches = false;
    out->target = 0;
    out->continues = false;
    snprintf(out->text, sizeof out->text, "defb %#.2x", byte);
    return 1;
}

uint8_t zdasm_decode(const uint8_t *code, size_t size, uint16_t pc, zdasm_insn *out)
{
    if (!code || !out || pc >= size)
    {
        return 0;
    }

    memset(out, 0, sizeof *out);

    const size_t left = size - pc;
    const uint8_t first = code[pc];

    uint32_t opcode = first;
    size_t length = 1;
    /* the displacement of a DDCB/FDCB instruction sits before its opcode byte */
    bool prefixed_index = false;
    uint8_t index_displacement = 0;

    if (0xDD == first || 0xFD == first)
    {
        if (left < 2)
        {
            return emit_defb(out, pc, first);
        }
        if (0xCB == code[pc + 1])
        {
            if (left < 4)
            {
                return emit_defb(out, pc, first);
            }
            index_displacement = code[pc + 2];
            prefixed_index = true;
            opcode = ((uint32_t)first << 16) | ((uint32_t)0xCB << 8) | code[pc + 3];
            length = 4;
        }
        else
        {
            opcode = ((uint32_t)first << 8) | code[pc + 1];
            length = 2;
        }
    }
    else if (0xCB == first || 0xED == first)
    {
        if (left < 2)
        {
            return emit_defb(out, pc, first);
        }
        opcode = ((uint32_t)first << 8) | code[pc + 1];
        length = 2;
    }

    const opcode_table *entry = find_opcode(opcode);
    if (!entry || !entry->mnemo)
    {
        return emit_defb(out, pc, first);
    }

    /* Operand bytes follow the opcode, except for DDCB/FDCB where the single
       "data" byte is the displacement already read above. */
    uint8_t operands[2] = {0};
    size_t operand_count = prefixed_index ? 0 : entry->data_size;
    if (is_double_argumented(opcode))
    {
        operand_count = 2;
    }

    if (left < length + operand_count)
    {
        return emit_defb(out, pc, first);
    }

    for (size_t i = 0; i < operand_count; ++i)
    {
        operands[i] = code[pc + length + i];
    }

    const uint16_t after = (uint16_t)(pc + length + operand_count);

    out->address = pc;
    out->length = (uint8_t)(length + operand_count);
    out->known = true;
    out->continues = control_continues(opcode);

    /* RST carries its target in the opcode rather than in an operand, so the
       operand-driven branch detection above never sees it. Left out, every RST
       vector looks unreachable - and RST 10 is how a Spectrum prints a
       character, so that is most of the ROM's entry points missed. */
    if (1u == out->length && 0xC7u == (opcode & 0xC7u))
    {
        out->branches = true;
        out->target = (uint16_t)(opcode & 0x38u);
    }

    if (prefixed_index)
    {
        snprintf(out->text, sizeof out->text, entry->mnemo, index_displacement);
    }
    else if (is_double_argumented(opcode))
    {
        snprintf(out->text, sizeof out->text, entry->mnemo, operands[0], operands[1]);
    }
    else if (entry->reljmp)
    {
        /* the displacement is signed and relative to the next instruction */
        out->branches = true;
        out->target = (uint16_t)(after + (int8_t)operands[0]);
        snprintf(out->text, sizeof out->text, entry->mnemo, out->target);
    }
    else if (2 == operand_count)
    {
        const uint16_t value = (uint16_t)(operands[0] | ((uint16_t)operands[1] << 8));
        if (is_absolute_jump(opcode) || is_call(opcode))
        {
            out->branches = true;
            out->target = value;
        }
        snprintf(out->text, sizeof out->text, entry->mnemo, value);
    }
    else if (1 == operand_count)
    {
        snprintf(out->text, sizeof out->text, entry->mnemo, operands[0]);
    }
    else
    {
        snprintf(out->text, sizeof out->text, "%s", entry->mnemo);
    }

    return out->length;
}

uint8_t zdasm_one(const uint8_t *code, size_t size, uint16_t pc, char *out, size_t out_size)
{
    zdasm_insn insn;
    const uint8_t used = zdasm_decode(code, size, pc, &insn);
    if (out && out_size)
    {
        snprintf(out, out_size, "%s", used ? insn.text : "");
    }
    return used;
}

size_t zdasm_range(const uint8_t *code, size_t size, uint16_t start, uint16_t end,
                   bool (*emit)(void *user, const zdasm_insn *insn), void *user)
{
    if (!emit)
    {
        return 0;
    }

    size_t emitted = 0;
    uint32_t pc = start;

    while (pc <= end)
    {
        zdasm_insn insn;
        const uint8_t used = zdasm_decode(code, size, (uint16_t)pc, &insn);
        if (!used)
        {
            break;
        }
        ++emitted;
        if (!emit(user, &insn))
        {
            break;
        }
        pc += used;
    }
    return emitted;
}
