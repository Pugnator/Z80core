/**
 *
 * @file   assembler.h
 * @date   16.03.2018 
 * @license This project is released under the GPL 2 license.
 * @brief 
 *
 */

#pragma once

#include <common.h>

extern RUNPASS run_pass;
extern uint16_t PC;
extern uint8_t prog[PROG_SIZE];

const opcode_table* find_opcode ( char* instruction );
void debug_print ( const char* format, ... );
void error_print ( const char* format, ... );
bool check_relative_jump ( intmax_t destination );
bool check_double_argumented ( uint16_t opcode );
bool is_single (uint32_t opcode);
bool is_prefixed (uint32_t opcode);
bool is_double_prefixed (uint32_t opcode);
int handle_instruction ( char* instruction, intmax_t data, size_t size );
void defw ( uint16_t data );
void deft ( char* text );
void add_label ( char* label, uint16_t address );
intmax_t get_label_address ( char* label );
void hex_print ( const void* pv, size_t len );
int load_file ( char* filename, char** buffer );
void print_labels ( user_label* print );
void cleanup ( void );
bool assembly_listing (const char *filename, const char *output_filename, const char *output_format );
int process_source ( const char* source, const char *fmt, FILE *out);
