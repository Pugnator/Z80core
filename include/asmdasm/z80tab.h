/**
 * @file   z80tab.h
 * @brief  Z80 instruction table interface
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
 *
 * This file is part of Z80core, released under the terms of the GNU General
 * Public License version 2. See LICENSE.md for the full text.
 */

#pragma once
#include <stdint.h>
#include <stdbool.h>

#define C_flag 1
#define N_flag 2
#define PV_flag 4
#define HF_flag 8
#define Z_flag 64
#define S_flag 128

typedef struct
{
    unsigned opcode : 24;
    unsigned data_size : 2;
    unsigned reljmp : 1;
    /* do not use while assembly */
    unsigned duplicate : 1;
    char *mnemo;
    unsigned flags;
} opcode_table;

extern const opcode_table opcode_tab[];
extern const int opcode_tab_count;
