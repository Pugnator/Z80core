/**
 *
 * @file   z80tab.h
 * @date   16.03.2018 
 * @license This project is released under the GPL 2 license.
 * @brief 
 *
 */

#pragma once
#include <stdint.h>
#include <stdbool.h>

#define C_flag  1
#define N_flag  2
#define PV_flag 4
#define HF_flag 8
#define Z_flag  64
#define S_flag  128

typedef struct
{
	unsigned opcode : 24;
	unsigned data_size : 2;
	unsigned reljmp : 1;
	char *mnemo;
	unsigned flags;
} opcode_table;

extern const opcode_table opcode_tab[];
extern const int opcode_tab_count;
