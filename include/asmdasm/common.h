#pragma once
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <strings.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
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
#pragma warn(disable: 2251 2145 2030 2154 2130 2135 )
#endif

int asprintf( char **, const char *, ... );
int vasprintf( char **, const char *, va_list );

//Safer asprintf macro
#define sasprintf(write_to,  ...) {           \
    char *tmp_string_for_extend = (write_to); \
    asprintf(&(write_to), __VA_ARGS__);       \
    free(tmp_string_for_extend);              \
}

#define MAX_SOURCE_SIZE 1000000L
#define PROG_SIZE 0xFFFF
#define fn_apply(type, fn, ...) { \
void *stopper_for_apply = (int[]){0}; \
type **list_for_apply = (type*[]){__VA_ARGS__, stopper_for_apply}; \
for (int i=0; list_for_apply[i] != stopper_for_apply; i++) \
fn(list_for_apply[i]); }

#define FREE(...) fn_apply(void, free, __VA_ARGS__);

typedef enum RUNPASS
{
    PASS1, PASS2, PASS3
}RUNPASS;

enum
{
    ASM_ERROR = 0, ASM_OK
};

typedef struct user_label
{
    int id;
    char *label;
    intmax_t address;
    UT_hash_handle hh;
}user_label;

typedef struct dereffered_label
{
    int id;
    char *label;
    intmax_t address;
    uint8_t size;
    UT_hash_handle hh;
}dereffered_label;

extern user_label *labels;
extern dereffered_label *dereffered;

extern int verbose;
extern int assembly;
extern int disassembly;
extern int current_line;
extern char* current_label;
extern bool abort_on_error;
extern bool tgt_label;
extern uint16_t PC;
extern uint16_t CURRENT_ORG;
extern uint8_t prog[PROG_SIZE];
extern RUNPASS run_pass;

#define NO_ARGS(x,y) if(!handle_instruction(x,0,0)){YYABORT;}free(y)
#define HANDLE(x,y,z) if(!handle_instruction(x,y,z)){YYABORT;}
#define TEMPLATE asprintf
#define UNSIGN8(x) \
if(x)\
{\
    if(x > 0xFF)\
    {\
        printf("Number %jd is too large, it will be truncated to fit 8bit\n", x);\
        x = 0xFF;\
    }\
}

#define SIGN8(x) \
if(x)\
{\
    if( x > 127 || x < -128)\
    {\
        printf("JR can only jump forward up to 127 and back to 128 bytes: %jd\n", x);\
        YYABORT;\
    }\
}


#define UNSIGN16(x) \
if(x)\
{\
    if(x > 0xFFFF)\
    {\
        printf("Number %jd is too large, it will be truncated to fit 16bit\n", x);\
        x = 0xFFFF;\
    }\
}


#define UNSIGN32(x) \
if(x)\
{\
    if(x > 0xFFFFFFFF)\
    {\
        printf("Number %jd is too large, it will be truncated to fit 32bit\n", x);\
        x = 0xFFFFFFFF;\
    }\
}

int asmlex(void);
void asmerror(const char *s);
void print_labels(user_label* print);
void asm_load_buffer (const char *input);
int handle_instruction (char *instruction, intmax_t data, size_t size);
void defw ( uint16_t data );
void deft ( char* text );
void add_label (char *label, uint16_t address);
intmax_t get_label_address (char *label);
int load_file (char *filename, char **buffer);
void hex_print(const void *pv, size_t len);
void code_generator ();
