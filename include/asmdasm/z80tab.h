#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct
{
	unsigned opcode : 24;
	unsigned data_size : 2;
	unsigned reljmp : 1;
	char *mnemo;
	char *hash;
} opcode_table;

extern const opcode_table opcode_tab[];
extern const int opcode_tab_count;

void generate_test_file();