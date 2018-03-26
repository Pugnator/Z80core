#pragma once
#include <stdint.h>
#include <stdio.h>

struct t_tap_info
{
	uint16_t prog_start;
	uint16_t entry_point;
	uint16_t rom_size;
	uint8_t  *rom;
};

int tap_create( struct t_tap_info *p_tap, FILE *out );
