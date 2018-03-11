#pragma once
#ifndef Z80_H
#define Z80_H
#include <stdint.h>

typedef struct
{
	unsigned opcode : 24;
	unsigned data_size : 2;
	unsigned reljmp : 1;
	char *mnemo;
	char *hash;
} opcode_table;

extern const opcode_table opcode_tab[];

#endif

