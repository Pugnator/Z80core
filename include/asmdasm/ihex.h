#pragma once
#include <stdint.h>
#include <stdio.h>

int save_array_to_ihex( FILE *ihex, int base, uint8_t b[], int size );
void *create_array_from_ihex( FILE *ihex, int *res );

