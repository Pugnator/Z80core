/**
 * @file   dassembler.c
 * @brief  Disassembler
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
 *
 * This file is part of Z80core, released under the terms of the GNU General
 * Public License version 2. See LICENSE.md for the full text.
 */

#include "dassembler.h"
#include "uthash.h"

/* Jump and call targets discovered on pass 1, printed as labels on pass 2.
   Keyed by address; the key type and size must match at add and find. */
typedef struct
{
    uint16_t address;
    UT_hash_handle hh;
} disasm_label;

static disasm_label *disasm_labels = NULL;

dsmctx *disasm_ctx_init(void)
{
    dsmctx *new = calloc(1, sizeof *new);
    return new;
}

void disasm_ctx_free(dsmctx *ctx)
{
    if (!ctx)
    {
        return;
    }
    free(CURRENT_DATA);
    free(ctx);
}

static void disasm_labels_free(void)
{
    disasm_label *cur, *tmp;
    HASH_ITER(hh, disasm_labels, cur, tmp)
    {
        HASH_DEL(disasm_labels, cur);
        free(cur);
    }
    disasm_labels = NULL;
}

bool disasm_is_a_prefix(uint8_t byte)
{
    switch (byte)
    {
    case 0xCB:
    case 0xDD:
    case 0xFD:
    case 0xED:
        return true;
    default:
        return false;
    }
}

const opcode_table *disasm_find_opcode(uint32_t instruction)
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

/* JP nn and its conditional forms */
bool disasm_is_abs_jump(uint32_t opcode)
{
    switch (opcode)
    {
    case 0xC3: /* JP nn */
    case 0xC2: /* JP NZ  */
    case 0xCA: /* JP Z   */
    case 0xD2: /* JP NC  */
    case 0xDA: /* JP C   */
    case 0xE2: /* JP PO  */
    case 0xEA: /* JP PE  */
    case 0xF2: /* JP P   */
    case 0xFA: /* JP M   */
        return true;
    default:
        return false;
    }
}

bool disasm_is_call(uint32_t opcode)
{
    switch (opcode)
    {
    case 0xCD: /* CALL nn */
    case 0xC4: /* CALL NZ */
    case 0xCC: /* CALL Z  */
    case 0xD4: /* CALL NC */
    case 0xDC: /* CALL C  */
    case 0xE4: /* CALL PO */
    case 0xEC: /* CALL PE */
    case 0xF4: /* CALL P  */
    case 0xFC: /* CALL M  */
        return true;
    default:
        return false;
    }
}

char *disasm_compile_string(const char *format, ...)
{
    if (!format)
    {
        return NULL;
    }
    va_list args;
    va_start(args, format);

    int size = vsnprintf(NULL, 0, format, args);
    va_end(args);

    if (size < 0)
    {
        return NULL;
    }

    char *string = malloc(size + 1);
    if (!string)
    {
        return NULL;
    }

    va_start(args, format);
    vsnprintf(string, size + 1, format, args);
    va_end(args);

    return string;
}

void disasm_add_label(uint16_t address)
{
    if (PASS1 != run_pass)
    {
        return;
    }

    disasm_label *lab = NULL;
    HASH_FIND(hh, disasm_labels, &address, sizeof(address), lab);
    if (lab)
    {
        return;
    }

    lab = malloc(sizeof(*lab));
    if (!lab)
    {
        puts("Memory allocation failed");
        return;
    }

    lab->address = address;
    HASH_ADD(hh, disasm_labels, address, sizeof(address), lab);
}

static bool disasm_has_label(uint16_t address)
{
    disasm_label *lab = NULL;
    HASH_FIND(hh, disasm_labels, &address, sizeof(address), lab);
    return NULL != lab;
}

/* LD (IX+d),n and LD (IY+d),n carry a displacement *and* an immediate */
static bool disasm_is_double_argumented(uint32_t opcode)
{
    return 0xDD36 == opcode || 0xFD36 == opcode;
}

static dsmopc *disasm_make_opcode(uint16_t address, char *mnemonic)
{
    dsmopc *opc = malloc(sizeof *opc);
    if (!opc)
    {
        free(mnemonic);
        return NULL;
    }
    opc->address = address;
    opc->mnemonic = mnemonic;
    return opc;
}

/**
@brief Decode the instruction at the current position and advance past it.
@return The decoded instruction, or NULL once the input is exhausted.

A byte that does not start a known instruction is emitted as a "defb" so the
listing stays complete, and decoding resumes at the following byte.
*/
dsmopc *disasm_fetch_next_opcode(dsmctx *ctx)
{
    if (CURRENT_PC >= ctx->data_size)
    {
        return NULL;
    }

    const uint16_t start = CURRENT_PC;
    const uint8_t *code = CURRENT_DATA;
    size_t left = ctx->data_size - start;
    uint8_t first = code[start];

    uint32_t opcode = first;
    size_t length = 1;
    /* the displacement of a DDCB/FDCB instruction sits before its opcode byte */
    bool prefixed_index = false;
    uint8_t index_displacement = 0;

    if (0xDD == first || 0xFD == first)
    {
        if (left < 2)
        {
            return disasm_make_opcode(start, disasm_compile_string("defb %#.2x", first));
        }
        if (0xCB == code[start + 1])
        {
            if (left < 4)
            {
                return disasm_make_opcode(start, disasm_compile_string("defb %#.2x", first));
            }
            index_displacement = code[start + 2];
            prefixed_index = true;
            opcode = ((uint32_t)first << 16) | ((uint32_t)0xCB << 8) | code[start + 3];
            length = 4;
        }
        else
        {
            opcode = ((uint32_t)first << 8) | code[start + 1];
            length = 2;
        }
    }
    else if (0xCB == first || 0xED == first)
    {
        if (left < 2)
        {
            return disasm_make_opcode(start, disasm_compile_string("defb %#.2x", first));
        }
        opcode = ((uint32_t)first << 8) | code[start + 1];
        length = 2;
    }

    const opcode_table *entry = disasm_find_opcode(opcode);
    if (!entry || !entry->mnemo)
    {
        /* unknown encoding: emit the leading byte and resynchronise on the next */
        CURRENT_PC = start + 1;
        return disasm_make_opcode(start, disasm_compile_string("defb %#.2x", first));
    }

    /* Operand bytes follow the opcode, except for DDCB/FDCB where the single
       "data" byte is the displacement that was already read above. */
    uint8_t operands[2] = {0};
    size_t operand_count = prefixed_index ? 0 : entry->data_size;
    if (disasm_is_double_argumented(opcode))
    {
        operand_count = 2;
    }

    if (left < length + operand_count)
    {
        CURRENT_PC = start + 1;
        return disasm_make_opcode(start, disasm_compile_string("defb %#.2x", first));
    }

    for (size_t i = 0; i < operand_count; ++i)
    {
        operands[i] = code[start + length + i];
    }
    CURRENT_PC = (uint16_t)(start + length + operand_count);

    char *text = NULL;
    if (prefixed_index)
    {
        text = disasm_compile_string(entry->mnemo, index_displacement);
    }
    else if (disasm_is_double_argumented(opcode))
    {
        text = disasm_compile_string(entry->mnemo, operands[0], operands[1]);
    }
    else if (entry->reljmp)
    {
        /* the displacement is signed and relative to the next instruction */
        uint16_t target = (uint16_t)(CURRENT_PC + (int8_t)operands[0]);
        disasm_add_label(target);
        text = disasm_compile_string(entry->mnemo, target);
    }
    else if (2 == operand_count)
    {
        uint16_t value = (uint16_t)(operands[0] | ((uint16_t)operands[1] << 8));
        if (disasm_is_abs_jump(opcode) || disasm_is_call(opcode))
        {
            disasm_add_label(value);
        }
        text = disasm_compile_string(entry->mnemo, value);
    }
    else if (1 == operand_count)
    {
        text = disasm_compile_string(entry->mnemo, operands[0]);
    }
    else
    {
        text = disasm_compile_string("%s", entry->mnemo);
    }

    return disasm_make_opcode(start, text);
}

void disasm_call_graph(void)
{
    disasm_label *cur, *tmp;
    HASH_ITER(hh, disasm_labels, cur, tmp)
    {
        printf("L%.4X\n", cur->address);
    }
}

int disasm_parse_input_stream(dsmctx *ctx)
{
    dsmopc *opcode = NULL;

    for (run_pass = PASS1; PASS2 >= run_pass; run_pass++)
    {
        /* every pass walks the whole input from the beginning */
        intmax_t opcode_ctr = ctx->opcodes_to_fetch;
        CURRENT_PC = 0;

        while ((opcode = disasm_fetch_next_opcode(ctx)))
        {
            if (0 == opcode_ctr)
            {
                free(opcode->mnemonic);
                free(opcode);
                break;
            }
            if (opcode_ctr > 0)
            {
                --opcode_ctr;
            }

            if (PASS2 == run_pass)
            {
                if (disasm_has_label(opcode->address))
                {
                    printf("L%.4X:\n", opcode->address);
                }
                printf("\t%s\t\t\t;%.4Xh\n", opcode->mnemonic ? opcode->mnemonic : "???", opcode->address);
            }
            free(opcode->mnemonic);
            free(opcode);
        }
    }
    return 1;
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
    long size = ftell(in);
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

    dsmctx *new = disasm_ctx_init();
    if (!new)
    {
        puts("Memory allocation failed");
        fclose(in);
        return false;
    }

    new->data_size = (size_t)size;
    new->prog = malloc(new->data_size ? new->data_size : 1);
    if (!new->prog)
    {
        puts("Memory allocation failed");
        disasm_ctx_free(new);
        fclose(in);
        return false;
    }

    if (fread(new->prog, 1, new->data_size, in) != new->data_size)
    {
        puts("Failed to read input file");
        disasm_ctx_free(new);
        fclose(in);
        return false;
    }
    fclose(in);

    new->opcodes_to_fetch = -1;
    new->bytes_to_parse = -1;
    printf("; %s, %zu bytes\n", source, new->data_size);

    disasm_parse_input_stream(new);

    disasm_ctx_free(new);
    disasm_labels_free();
    return true;
}
