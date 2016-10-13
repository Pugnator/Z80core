#ifndef DISASM_H
#define DISASM_H
#define _GNU_SOURCE
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <stdlib.h>
#include <inttypes.h>
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <common.h>
#include "z80tab.h"

#define INC_PC (ctx->PC++)
#define CURRENT_PC (ctx->PC)
#define CURRENT_BYTE (ctx->opcode.byte[opcode_byte_counter])
#define CURRENT_INSTRUCTION (ctx->opcode.instruction)
#define CURRENT_DATA (ctx->prog)

#define SWAP16(x) ((x & 0xff) << 8) | ((x & 0xff00) >> 8)
#define SWAP32(x) x = (x & 0x0000FFFF) << 16 | (x & 0xFFFF0000) >> 16;\
x = (x & 0x00FF00FF) << 8 | (x & 0xFF00FF00) >> 8;

enum
{
	NO_MORE_DATA = 0, OK, ERROR
};

typedef struct dsmctx
{
	uint16_t PC;
	uint8_t* prog;
	size_t data_size;
	intmax_t opcodes_to_fetch;
	intmax_t bytes_to_parse;
	bool to_stdout;
	char* mnemo;
	union
	{
		uint32_t instruction;
#pragma pack(4)
		struct
		{
			uint8_t byte[4];
		};
	} opcode;
	union
	{
		uint16_t data;
#pragma pack(2)
		struct
		{
			uint8_t byte[2];
		};
	} data;
	uint8_t opcode_size;
} dsmctx;

typedef struct dsmopc
{
	uint16_t address;
	char* mnemonic;
} dsmopc;

dsmctx* disasm_ctx_init ( void );
void disasm_ctx_free ( dsmctx* ctx );
bool disasm_is_a_prefix ( uint8_t byte );
void disasm_byte_swap ( char* p, int len );
const char* disasm_find_opcode ( uint32_t instruction, int8_t* data_size );
bool disasm_is_an_abs_jump ( uint32_t opcode );
bool disasm_is_an_rel_jump ( uint32_t opcode );
char* disasm_compile_string ( const char* format, ... );
dsmopc* disasm_fetch_next_opcode ( dsmctx* ctx );
int disasm_parse_input_stream ( dsmctx* ctx );


#endif
