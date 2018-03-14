#include "dassembler.h"

dsmctx* disasm_ctx_init ( void )
{
	dsmctx* new = calloc ( 1,sizeof *new );
	return new;
}

void disasm_ctx_free ( dsmctx* ctx )
{
	free ( CURRENT_DATA );
	free ( ctx );
}

bool disasm_is_a_prefix ( uint8_t byte )
{
	switch ( byte )
	{
		case 0xCB:
		case 0xDD:
		case 0xFD:
		case 0xED:
			return 1;
		default:
			return 0;
	}
}

void disasm_byte_swap ( char* p, int len )
{
	int i;
	char tmp;
	for ( i = 0; i < len/2; i++ )
	{
		tmp = p[len-i-1];
		p[len-i-1] = p[i];
		p[i] = tmp;
	}
}

const char* disasm_find_opcode ( uint32_t instruction, int8_t* data_size )
{
	for ( int i=0; opcode_tab[i].mnemo; i++ )
	{
		if ( instruction == opcode_tab[i].opcode )
		{
			*data_size = opcode_tab[i].data_size;
			return ( opcode_tab[i].mnemo );
		}
	}
	return "";
}

bool disasm_is_an_abs_jump ( uint32_t opcode )
{
	switch ( opcode )
	{
		case 0xC2:
		case 0xC3:
			return true;
		default:
			return false;
	}
}

bool disasm_is_an_rel_jump ( uint32_t opcode )
{
	switch ( opcode )
	{
		case 0x10:
		case 0x18:
		case 0x20:
		case 0x28:
		case 0x30:
		case 0x38:
			return true;
			break;
		default:
			return false;
	}
}

char* disasm_compile_string ( const char* format, ... )
{
	char* string;
	va_list args;
	va_start ( args, format );
	if ( 0 > vasprintf ( &string, format, args ) ) string = NULL;
	va_end ( args );
	return string;
}

dsmopc* disasm_fetch_next_opcode ( dsmctx* ctx )
{
	if ( CURRENT_PC >= ctx->data_size )
		return NULL;
		
	int8_t opcode_byte_counter=0;
	bool opcode_prefixed = false;
	int8_t data_size = 0;
	
	dsmopc* opc = malloc ( sizeof *opc );
	opc->address = CURRENT_PC;
	
	while ( disasm_is_a_prefix ( CURRENT_DATA[CURRENT_PC] ) )
	{
		ctx->opcode.byte[opcode_byte_counter++]=CURRENT_DATA[INC_PC];
		opcode_prefixed = true;
		if ( CURRENT_PC >= ctx->data_size )
		{
			free ( opc );
			return NULL;
		}
	}
	if ( opcode_prefixed && 1 == opcode_byte_counter )
	{
		CURRENT_BYTE=CURRENT_DATA[INC_PC];
		disasm_byte_swap ( ctx->opcode.byte, 2 );
		const char* mnemo = disasm_find_opcode ( CURRENT_INSTRUCTION, &data_size );
		if ( data_size )
		{
			int8_t data_counter=0;
			while ( data_counter < data_size )
			{
				if ( CURRENT_PC >= ctx->data_size )
				{
					free ( opc );
					return NULL;
				}
				ctx->data.byte[data_counter++]=CURRENT_DATA[INC_PC];
			}
			opc->mnemonic = disasm_compile_string ( mnemo, ctx->data.data );
			return opc;
		}
		opc->mnemonic = disasm_compile_string ( "%s", mnemo );
		return opc;
	}
	if ( opcode_prefixed && 2 == opcode_byte_counter )
	{
		if ( CURRENT_PC +1 >= ctx->data_size )
		{
			free ( opc );
			return NULL;
		}
		CURRENT_BYTE=CURRENT_DATA[CURRENT_PC + 1];
		uint8_t temp[4] = {0};
		memcpy ( temp, ctx->opcode.byte, 4 );
		disasm_byte_swap ( ctx->opcode.byte, 3 );
		const char* mnemo = disasm_find_opcode ( CURRENT_INSTRUCTION, &data_size );
		memcpy ( ctx->opcode.byte, temp, 4 );
		ctx->opcode.byte[opcode_byte_counter++]=CURRENT_DATA[INC_PC];
		if ( CURRENT_PC >= ctx->data_size )
		{
			free ( opc );
			return NULL;
		}
		CURRENT_BYTE=CURRENT_DATA[INC_PC];
		disasm_byte_swap ( ctx->opcode.byte, 4 );
		opc->mnemonic = disasm_compile_string ( mnemo, ctx->opcode.byte[1] );
		return opc;
	}
	else
	{
		ctx->opcode.byte[opcode_byte_counter++]=CURRENT_DATA[INC_PC];
		const char* mnemo = disasm_find_opcode ( CURRENT_INSTRUCTION, &data_size );
		if ( data_size )
		{
			int8_t data_counter=0;
			while ( data_counter < data_size )
			{
				if ( CURRENT_PC >= ctx->data_size )
				{
					free ( opc );
					return NULL;
				}
				ctx->data.byte[data_counter++]=CURRENT_DATA[INC_PC];
			}
			
			if ( disasm_is_an_rel_jump ( CURRENT_INSTRUCTION ) )
			{
				int16_t addr = ( int8_t ) ctx->data.data + CURRENT_PC;
				opc->mnemonic = disasm_compile_string ( mnemo, addr );
			}
			else
			{
				opc->mnemonic = disasm_compile_string ( mnemo, ctx->data.data );
			}
			return opc;
		}
		opc->mnemonic =  disasm_compile_string ( "%s", mnemo );
		return opc;
	}
	
}

int disasm_parse_input_stream ( dsmctx* ctx )
{
	dsmopc* opcode = 0;
	intmax_t opcode_ctr = ctx->opcodes_to_fetch;
	while ( ( opcode = disasm_fetch_next_opcode ( ctx ) ) )
	{
		if ( ! opcode_ctr-- )
			break;
		printf ( "%s ", opcode->mnemonic );
		free ( opcode->mnemonic );
		printf ( "\t\t\t;%#.4x\n", CURRENT_PC - 1 );
		free ( opcode );
		CURRENT_INSTRUCTION=0;
		ctx->data.data=0;
		ctx->opcode_size=0;
	}
	return 1;
}

void disassembly_listing (char *source)
{
	puts("Starting disasm");
	intmax_t opcodes_to_fetch = -1;
	intmax_t bytes_to_parse = -1;
	dsmctx* new = disasm_ctx_init();
	FILE* in = fopen(source, "r");
	assert(in);
	fseek(in, 0, SEEK_END);
	new->data_size = ftell(in);
	rewind(in);
	new->prog = malloc(new->data_size);
	(void)fread(new->prog, 1, new->data_size, in);
	new->opcodes_to_fetch = opcodes_to_fetch;
	new->bytes_to_parse = bytes_to_parse;

	disasm_parse_input_stream(new);
	printf(";Parsed %zu bytes\n", new->data_size);
	disasm_ctx_free(new);
	fclose(in);
}

#if 0
int main(int argc, char** argv)
{

	char* source_file = NULL;
	char* output_file = NULL;
	int32_t opt = 0;
	intmax_t opcodes_to_fetch = -1;
	intmax_t bytes_to_parse = -1;
	if (argc == 1)
	{
		return (EXIT_FAILURE);
	}
	while ((opt = getopt(argc, argv, "o:n:f:")) != -1)
	{
		switch (opt)
		{
		case 'o':
			output_file = optarg;
			break;
		case 'f':
			opcodes_to_fetch = strtoimax(optarg, 0, 10);
			if (opcodes_to_fetch <= 0)
			{
				opcodes_to_fetch = 0;
			}
			break;
		case 'n':
			bytes_to_parse = strtoimax(optarg, 0, 10);
			if (bytes_to_parse <= -2)
			{
				bytes_to_parse = 0;
			}
			break;
		}
	}

	for (int index = optind; index < argc; index++)
		source_file = argv[index];

	dsmctx* new = disasm_ctx_init();
	FILE* in = fopen(source_file, "r");
	assert(in);
	fseek(in, 0, SEEK_END);
	new->data_size = ftell(in);
	rewind(in);
	new->prog = malloc(new->data_size);
	fread(new->prog, 1, new->data_size, in);
	new->opcodes_to_fetch = opcodes_to_fetch;
	new->bytes_to_parse = bytes_to_parse;

	disasm_parse_input_stream(new);
	printf(";Parsed %zu bytes\n", new->data_size);
	disasm_ctx_free(new);
	fclose(in);
}

#endif // 0
