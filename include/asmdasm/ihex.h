/**
 * @file   ihex.h
 * @brief  Intel HEX reader and writer interface
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

int save_array_to_ihex(FILE *ihex, int base, uint8_t b[], int size);
void *create_array_from_ihex(FILE *ihex, int *res);
