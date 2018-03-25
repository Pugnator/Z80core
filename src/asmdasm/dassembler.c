/**
 *
 * @file   dassembler.c 
 * @date   16.03.2018 
 * @license This project is released under the GPL 2 license.
 * @brief Disassembler
 *
 */
 
#include "dassembler.h"
#include "uthash.h"

dsmctx* disasm_ctx_init ( void )
{
	dsmctx* new = calloc ( 1,sizeof *new );
	return new;
}

typedef struct 
{
  int id;          
  UT_hash_handle hh;
}disasm_label;

disasm_label *disasm_labels = NULL;

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
	return NULL;
}

bool disasm_is_abs_jump ( uint32_t opcode )
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

bool disasm_is_return ( uint32_t opcode )
{
	switch ( opcode )
	{
		case 0xC9:
    case 0xD8:
    case 0xF8:
    case 0xD0:
    case 0xC0:
    case 0xF0:
    case 0xE8:
    case 0xE0:
    case 0xC8:
    case 0xED4D:
    case 0xED45:
    case 0xED55:
    case 0xED65:
    case 0xED5D:
    case 0xED75:
    case 0xED6D:
    case 0xED7D:
			return true;
		default:
			return false;
	}
}

bool disasm_is_rel_jump ( uint32_t opcode )
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

bool disasm_is_call ( uint32_t opcode )
{
	switch ( opcode )
	{
		case 0x10:
    case 0xCD:
    case 0xDC:
    case 0xFC:
    case 0xD4:
    case 0xC4:
    case 0xF4:
    case 0xEC:
    case 0xE4:
    case 0xCC:
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

int32_t disasm_add_label(uint16_t address)
{  
  disasm_label* lab = NULL;
  HASH_FIND_INT ( disasm_labels, &address, lab );
  if ( lab )
  {
    if(PASS2 == run_pass)
    {
      return lab->id;
    }
    return -1;
  }
  if(PASS2 == run_pass)
  {
    return -1;
  }
  lab = (disasm_label*)malloc(sizeof(disasm_label));
  lab->id = address;
  HASH_ADD_INT( disasm_labels, id, lab);
  return -1;
}

dsmopc* disasm_fetch_next_opcode ( dsmctx* ctx )
{
	if ( CURRENT_PC >= ctx->data_size )
  {
		return NULL;
  }
  
  if (PASS2 == run_pass)
  {
    disasm_label* lab = NULL;
    HASH_FIND_INT ( disasm_labels, &CURRENT_PC, lab );
    if(lab)
    {
      printf("L%.4X:\n", lab->id & 0xFFFF);
    }
    else
    {
      printf("Label not found at %.4X\n", CURRENT_PC);
    }
  }
  
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
		CURRENT_BYTE = CURRENT_DATA[CURRENT_PC + 1];
		uint8_t temp[4] = {0};
		memcpy ( temp, ctx->opcode.byte, 4 );
		disasm_byte_swap ( ctx->opcode.byte, 3 );
		const char* mnemo = disasm_find_opcode ( CURRENT_INSTRUCTION, &data_size );
		memcpy ( ctx->opcode.byte, temp, 4 );
		ctx->opcode.byte[opcode_byte_counter++] = CURRENT_DATA[INC_PC];
		if ( CURRENT_PC >= ctx->data_size )
		{
			free ( opc );
			return NULL;
		}
		CURRENT_BYTE = CURRENT_DATA[INC_PC];
		disasm_byte_swap ( ctx->opcode.byte, 4 );
		opc->mnemonic = disasm_compile_string ( mnemo, ctx->opcode.byte[1] );
		return opc;
	}
	else
	{
		ctx->opcode.byte[opcode_byte_counter++] = CURRENT_DATA[INC_PC];
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
				ctx->data.byte[data_counter++] = CURRENT_DATA[INC_PC];
			}   
			
      /* Check if it is a jump instruction */
      if ( disasm_is_rel_jump ( CURRENT_INSTRUCTION ))
      {
        int32_t addr = disasm_add_label(ctx->data.data + CURRENT_PC);     
        opc->mnemonic = disasm_compile_string ( mnemo, ctx->data.data + CURRENT_PC);
      }
      else if (disasm_is_abs_jump ( CURRENT_INSTRUCTION ) || disasm_is_call(CURRENT_INSTRUCTION))
      {
        int32_t addr = disasm_add_label(ctx->data.data);        
        opc->mnemonic = disasm_compile_string ( mnemo, ctx->data.data );
      }
      else
      {
        opc->mnemonic = disasm_compile_string ( mnemo, ctx->data.data );
      }			
			return opc;
		}    
    
    if (disasm_is_return( CURRENT_INSTRUCTION ))
    {
     
    }
		opc->mnemonic =  disasm_compile_string ( "%s", mnemo );
		return opc;
	}
	
}

void disasm_call_graph()
{
  disasm_label *cur, *tmp;
  HASH_ITER ( hh, disasm_labels, cur, tmp )
  {
    printf ( "L%.4X\n", cur->id);
  }
}

int disasm_parse_input_stream ( dsmctx* ctx )
{
	dsmopc* opcode = 0;
  /* PASS 1 */
	intmax_t opcode_ctr = ctx->opcodes_to_fetch;
  for(run_pass = PASS1; PASS2 >= run_pass ; run_pass++)
  {
    CURRENT_PC = 0;
    CURRENT_INSTRUCTION=0;
    ctx->data.data=0;
    ctx->opcode_size=0;
    while ( ( opcode = disasm_fetch_next_opcode ( ctx ) ) )
    {
      if ( ! opcode_ctr-- )
      {
        break;
      }
      if (PASS2 == run_pass)
      {       
        printf ( "%s\t\t\t;%.4Xh\n", opcode->mnemonic, CURRENT_PC - 1);
      }
      free ( opcode->mnemonic );
      free ( opcode );
      CURRENT_INSTRUCTION=0;
      ctx->data.data=0;
      ctx->opcode_size=0;      
    }
  }
  disasm_call_graph();
	return 1;
}


void disassembly_buffer (char *buffer)
{
  run_pass = PASS1;
	intmax_t opcodes_to_fetch = -1;
	intmax_t bytes_to_parse = -1;
	dsmctx* new = disasm_ctx_init();	
	new->opcodes_to_fetch = opcodes_to_fetch;
	new->bytes_to_parse = bytes_to_parse;
	disasm_parse_input_stream(new);  
	disasm_ctx_free(new); 
}

void disassembly_listing (char *source)
{
  run_pass = PASS1;
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
  
	disasm_ctx_free(new);
	fclose(in);
 
}