/**
 * @file   assembler.h
 * @brief  Assembler interface
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
 *
 * This file is part of Z80core, released under the terms of the GNU General
 * Public License version 2. See LICENSE.md for the full text.
 */

#pragma once

#include <common.h>

extern RUNPASS run_pass;
extern uint16_t PC;
extern uint8_t prog[PROG_SIZE];

const opcode_table *find_opcode(char *instruction);
void debug_print(const char *format, ...);
void error_print(const char *format, ...);
bool check_relative_jump(intmax_t destination);
bool check_double_argumented(uint16_t opcode);
bool is_single(uint32_t opcode);
bool is_prefixed(uint32_t opcode);
bool is_double_prefixed(uint32_t opcode);
int handle_instruction(char *instruction, intmax_t data, size_t size);
void defw(intmax_t data);
void deft(char *text);
void add_label(char *label, intmax_t address);
intmax_t get_label_address(char *label);
void hex_print(const void *pv, size_t len);
int load_file(char *filename, char **buffer);
void print_labels(user_label *print);
bool assembly_listing(char *filename, char *output_filename, char *output_format);
int process_source(char *source, char *fmt, FILE *out);
