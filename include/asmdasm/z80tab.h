#pragma once
#ifndef Z80_H
#define Z80_H
#include <stdint.h>

typedef struct
{
	uint32_t opcode : 24;
	uint8_t data_size : 2;
	uint8_t reljmp : 1;
	char *mnemo;
	char *hash;
} opcode_table;

extern const opcode_table opcode_tab[];

#endif

