/**
 *
 * @file   common.h
 * @date   16.03.2018
 * @license This project is released under the GPL 2 license.
 * @brief
 *
 */

#pragma once
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <strings.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdlib.h>
#include <assert.h>
#include <getopt.h>
#include <fcntl.h>
#include <math.h>
#include <z80tab.h>
#include <text.h>
#include "uthash.h"
#include <grammar.h>

#ifndef __GNUC__
/* temporary solution */
#pragma warn(disable : 2251 2145 2030 2154 2130 2135)
#endif

int asprintf(char **, const char *, ...);
int vasprintf(char **, const char *, va_list);

// Safer asprintf macro
#define sasprintf(write_to, ...)                                                                                       \
    {                                                                                                                  \
        char *tmp_string_for_extend = (write_to);                                                                      \
        asprintf(&(write_to), __VA_ARGS__);                                                                            \
        free(tmp_string_for_extend);                                                                                   \
    }

#define MAX_SOURCE_SIZE 1000000L
/* the Z80 address space: valid addresses are 0x0000..0xFFFF inclusive */
#define PROG_SIZE 0x10000
#define fn_apply(type, fn, ...)                                                                                        \
    {                                                                                                                  \
        void *stopper_for_apply = (int[]){0};                                                                          \
        type **list_for_apply = (type *[]){__VA_ARGS__, stopper_for_apply};                                            \
        for (int i = 0; list_for_apply[i] != stopper_for_apply; i++)                                                   \
            fn(list_for_apply[i]);                                                                                     \
    }

#define FREE(...) fn_apply(void, free, __VA_ARGS__);

typedef enum RUNPASS
{
    PASS1,
    PASS2,
    PASS3
} RUNPASS;

enum
{
    ASM_ERROR = 0,
    ASM_OK
};

/* Labels and EQU constants. Keyed by the lowercased name; an address of
   INTMAX_MIN marks an EQU whose value is not resolvable yet. */
typedef struct user_label
{
    char *label;
    intmax_t address;
    /* pass this definition was last seen on, to catch redefinitions */
    unsigned pass_defined;
    UT_hash_handle hh;
} user_label;

extern user_label *labels;

extern int verbose;
extern int assembly;
extern int disassembly;
extern int current_line;
extern bool abort_on_error;
/* set by get_label_address() when an expression referenced an undefined label */
extern bool label_unresolved;
extern uint16_t PC;
extern uint16_t DATA_PC;
extern uint16_t CURRENT_ORG;
extern uint16_t PROG_START;
extern uint8_t prog[PROG_SIZE];
extern size_t assembled_bytes;
extern RUNPASS run_pass;

#define NO_ARGS(x, y)                                                                                                  \
    if (!handle_instruction(x, 0, 0))                                                                                  \
    {                                                                                                                  \
        YYABORT;                                                                                                       \
    }
#define HANDLE(x, y, z)                                                                                                \
    if (!handle_instruction(x, y, z))                                                                                  \
    {                                                                                                                  \
        YYABORT;                                                                                                       \
    }
#define TEMPLATE(_xbufr, ...) snprintf(_xbufr, MAX_TOKEN_SIZE, __VA_ARGS__)
#define UNSIGN8(x)                                                                                                     \
    if (x)                                                                                                             \
    {                                                                                                                  \
        if (x > 0xFF)                                                                                                  \
        {                                                                                                              \
            printf("Number %jd is too large, it will be truncated to fit 8bit\n", x);                                  \
            x = 0xFF;                                                                                                  \
        }                                                                                                              \
    }

/* Index displacement. The Z80 encodes it as a signed byte, but 0x80..0xFF is
   accepted as the raw byte too: that is how the disassembler prints negative
   displacements, so its output can be fed back in. */
#define SIGN8(x)                                                                                                       \
    if (x > 0xFF || x < -128)                                                                                          \
    {                                                                                                                  \
        printf("Index displacement %jd is out of range [-128..255]\n", x);                                             \
        YYABORT;                                                                                                       \
    }

#define UNSIGN16(x)                                                                                                    \
    if (x)                                                                                                             \
    {                                                                                                                  \
        if (x > 0xFFFF)                                                                                                \
        {                                                                                                              \
            printf("Number %jd is too large, it will be truncated to fit 16bit\n", x);                                 \
            x = 0xFFFF;                                                                                                \
        }                                                                                                              \
    }

#define UNSIGN32(x)                                                                                                    \
    if (x)                                                                                                             \
    {                                                                                                                  \
        if (x > 0xFFFFFFFF)                                                                                            \
        {                                                                                                              \
            printf("Number %jd is too large, it will be truncated to fit 32bit\n", x);                                 \
            x = 0xFFFFFFFF;                                                                                            \
        }                                                                                                              \
    }

int asmlex(void);
void asmerror(const char *s);
/* diagnostics; a reported error fails the assembly (see assembler.c) */
void error_print(const char *format, ...);
void error_print_early(const char *format, ...);
void debug_print(const char *format, ...);
void print_labels(user_label *print);
void asm_load_buffer(const char *input);
int handle_instruction(char *instruction, intmax_t data, size_t size);
void defw(intmax_t data);
void defb(intmax_t data);
void deft(char *text);
void add_label(char *label, intmax_t address);
void set_origin(intmax_t address);
intmax_t divide_expr(intmax_t left, intmax_t right);
intmax_t modulo_expr(intmax_t left, intmax_t right);
intmax_t shift_expr(intmax_t value, intmax_t count, bool left);
intmax_t get_label_address(char *label);
int load_file(char *filename, char **buffer);
void hex_print(const void *pv, size_t len);
