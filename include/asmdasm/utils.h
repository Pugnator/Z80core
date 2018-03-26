#pragma once
#include <stdint.h>

/* remove ws from end of string, string must be mutable */
char *rtrim( char *s );
/* remove ws from start of string */
char *ltrim( char *s );
/* check if string is hex digital */
int isxstring( char *s );
/* convert hex char to value */
unsigned char chx2v(char a);
/* convert 2 chars from hex string to byte */
unsigned char sx2v( char *s );
/* shrink hex string to binary use same buffer */
int shrink_xs2bin( char *io );

/* unpack 16bit from byte array, big endian */
uint16_t u16_be( uint8_t *b );
void u16_be_pack( uint8_t *b, uint16_t val );
/* unpack 32bit from byte array, big endian */
uint32_t u32_be( uint8_t *b );
uint32_t u24_be( uint8_t *b );
/* same for little endian */
uint16_t u16_le( uint8_t *b );
uint32_t u32_le( uint8_t *b );
uint32_t u24_le( uint8_t *b );
