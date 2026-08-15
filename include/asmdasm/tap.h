/**
 * @file   tap.h
 * @brief  ZX Spectrum .tap image writer interface
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
 *
 * This file is part of Z80core, released under the terms of the GNU General
 * Public License version 2. See LICENSE.md for the full text.
 */

#pragma once
#include <stdint.h>
#include <stdio.h>

struct t_tap_info
{
    uint16_t prog_start;
    uint16_t entry_point;
    uint16_t rom_size;
    uint8_t *rom;
};

int tap_create(struct t_tap_info *p_tap, FILE *out);
