/**
 *
 * @file   assembler.c 
 * @date   16.03.2018 
 * @license This project is released under the GPL 2 license.
 * @brief Assembler
 *
 */

#include <common.h>
#include <assembler.h>

RUNPASS run_pass = PASS1;
char* current_label;
bool tgt_label = false;
bool abort_on_error = true;
uint16_t PC = 0;
uint16_t DATA_PC = 0;
uint16_t CURRENT_ORG = 0;
uint8_t prog[PROG_SIZE]= {0};
user_label* labels = NULL;
dereffered_label* dereffered = NULL;

enum
{
    ERROR_RELJM_RANGE=0, ERROR_MNEM_NOT_FOUND, ERROR_LABEL_EXISTS
};

const char* error_texts[]=
{
    "Relative jump is out of range: %jd [must be between -126:129]\n",
    "Mnemonic was not found: \"%s\"\n",
    "Label with the same address already declared",
    NULL
};

/* use binary search algorithm, be sure that opcode table sorted by mnemo field */
const opcode_table* find_opcode( char *instruction )
{
  int i, i_low, i_high, cmp;
  
  /* get latest valid index */
  i_high = opcode_tab_count-1;
  i_low = cmp = i = 0;

  /* find sorted array entry */
  while( i_low <= i_high )
	{
		i = ( i_low + i_high )/2;
    cmp = strcasecmp( instruction, opcode_tab[i].mnemo );
    if( !cmp )
      cmp = 0-opcode_tab[i].duplicate;
		if( !cmp )
			return &opcode_tab[i];
		if ( cmp < 0 )
			i_high = i - 1;
		else
			i_low = i + 1;
	}
  return (void*)0;
}

void debug_print ( const char* format, ... )
{
    if ( !verbose )
        return;
    char* string;
    va_list args;
    va_start ( args, format );
    if ( 0 > vasprintf ( &string, format, args ) ) string = NULL;
    va_end ( args );
    printf ( "Line %d: %s", current_line, string );
    free ( string );
}

void error_print ( const char* format, ... )
{
    char* string;
    va_list args;
    va_start ( args, format );
    if ( 0 > vasprintf ( &string, format, args ) ) string = NULL;
    va_end ( args );
    printf ( "Line %d: %s", current_line, string );
    free ( string );
}

bool check_relative_jump ( intmax_t destination )
{
    return destination <= 127 && destination >= -128;
}

bool check_double_argumented ( uint16_t opcode )
{
    switch ( opcode )
    {
        case 0xDD36:
        case 0xFD36:
            return true;
        default:
            return false;
    }
}

bool is_single (uint32_t opcode)
{
    return opcode <= 0xFF;
}

bool is_prefixed (uint32_t opcode)
{
    return opcode <= 0xFFFF;
}

bool is_double_prefixed (uint32_t opcode)
{
    return opcode > 0xFFFF;
}

/**
@brief Main function that handles instruction extracted by Bison.
@param result Resulted template of instruction created by Bison's grammar rule
@param data Immediate data for instruction if needed.
@param size Size of the data parameter in bytes. If equals 0 - no data present.
@return 1 on success, 0 on error
*/
int handle_instruction ( char* instruction, intmax_t data, size_t size )
{
    if('*' == *instruction)
    {
        return ASM_ERROR;
    }
    if ( PASS1 == run_pass )
    {
        data=0;
    }

    const opcode_table* new_opc = find_opcode ( instruction );
    if ( NULL == new_opc )
    {
        if ( PASS2 == run_pass )
        {
            error_print ( error_texts[ERROR_MNEM_NOT_FOUND], instruction );
            return ASM_ERROR;
        }
        else
        {
            return ASM_OK;
        }
    }
    /* Handle cases where relative jump is pointed to label */
    if ( new_opc->reljmp && PASS2 == run_pass && !check_relative_jump ( data ) )
    {
        error_print ( error_texts[ERROR_RELJM_RANGE], data );
        return ASM_ERROR;
    }
    /* If relative jump is pointed to a label */
    if ( true == tgt_label && new_opc->reljmp )
    {
        tgt_label = false;
    }
    /* Basic one-byte opcodes */
    if ( is_single(new_opc->opcode) )
    {
        prog[PC++] = new_opc->opcode & 0xFF;
    }
    /* Single prefixed */
    else if ( is_prefixed( new_opc->opcode ))
    {
        prog[PC++] = new_opc->opcode >> 8;
        prog[PC++] = new_opc->opcode & 0xFF;
    }
    /* Double prefixed */
    else
    {
        if ( PASS2 == run_pass && verbose )
        {
            printf ( "%#.4x: ", PC );
            printf ( new_opc->mnemo, data );
            puts ( "" );
        }
        prog[PC++] = new_opc->opcode >> 16;
        prog[PC++] = new_opc->opcode >> 8;
        prog[PC++] = data & 0xFF;
        prog[PC++] = new_opc->opcode & 0xFF;
        return 1;
    }

    /* IMMEDIATE DATA HANDLING */

    /*
    Data is INTMAX_MIN when the data is actually a label and not found at
    translating time.
    */
    if ( INTMAX_MIN == data )
    {
        /*
        At the moment PC points to the memory data should be placed too.
        We should add real address and its size (for JR/DJNZ like instructions)
        into dereffered table
        */
        dereffered_label* new = NULL;
        uint16_t address = PC;
        HASH_FIND_INT ( dereffered, &address, new );
        if ( new )
        {
            error_print ( error_texts[ERROR_LABEL_EXISTS] );
            return ASM_ERROR;
        }
        new = malloc ( sizeof *new );
        new->address = address;
        new->size = size;
        PC+=size;
        new->label = current_label;
        HASH_ADD_INT ( dereffered, address, new );
        return ASM_OK;
    }

    if ( !size )
    {
        if ( PASS2 == run_pass && verbose )
        {
            printf ( "%#.4x: ", PC - 1 );
            printf ( "%s\n", new_opc->mnemo );
        }
        return ASM_OK;
    }
    if ( PASS2 == run_pass && verbose )
    {
        printf ( "%#.4x: ", PC - 1 );
        printf ( new_opc->mnemo, ( uint16_t ) data );
        puts ( "" );
    }
    prog[PC++] = ( uint8_t ) data & 0xFF;
    if ( 2 == size || check_double_argumented ( new_opc->opcode ) )
    {
        prog[PC++] = ( uint8_t ) ( data >> 8 ) & 0xFF;
    }
    return ASM_OK;
}

/**
 * [Define word(s)]
 * @param data [description]
 */
void defw ( uint16_t data )
{
  prog[DATA_PC++] = data & 0xFF;
  prog[DATA_PC++] = (data>>8) & 0xFF;
}

void defb ( uint16_t data )
{
  prog[DATA_PC++] = data & 0xFF;
}

static uint8_t hex2val(char a)
{
  return (a > '9') ?
  (uint8_t) ((a & 0xDFu) - 0x37u):
  (uint8_t) (a - '0');
}

static char process_backslash( char *s, int *index ) 
{
	switch( *s )
  {
   case 'a':
     return '\a';
   case 'b':
     return '\b';
   case 'f':
     return '\f';
   case 'n':
     return '\n';
   case 'r':
     return '\r';
   case 't':
     return '\t';
   case 'v':
     return '\v';
   case '\'':
   case '"':
   case '\\':
   case '\?':
     break;
   /* hex value */
   case 'x':
   {
    int val = 0;
    for( int i = 1; isxdigit( s[i] ); ++i )
    {
      ++index[0];
      val<<=4;
      val |= hex2val( s[i] );
    }
    return val&0xFF;
   }
   /* oct value */
   case '0':
   case '1':
   case '2':
   case '3':
   case '4':
   case '5':
   case '6':
   case '7':
   {
    int val = 0;
    /* put back */
    --index[0];
    for( int i = 0; i < 3; ++i )
    {
      if( s[0] < '0' || s[0] > '7' )
        break;
      val<<=3;
      val |= *s-'0';
      ++s;
      ++index[0];
    }
    return val;
   }
  }
  return s[0];
}

/**
 * [deft  description]
 * @param text [description]
 */
void deft ( char* text )
{
  int len = strlen( text );
  int escape = 0;
  for( int i = 0; i < len; ++i )
  {
    int ch = text[i];
    if( !escape )
    {
      if( ch == '\"' )
        continue;
      if( ch == '\\' )
      {
        escape = 1;
        continue;
      }
    }
    else
    {
      escape = 0;
      ch = process_backslash( &text[i], &i );
    }
    prog[DATA_PC++] = ch;
  }
}

/**
 * [add_label  description]
 * @param label   [description]
 * @param address [description]
 */
void add_label ( char* label, uint16_t address )
{
    if ( PASS1 != run_pass )
        return;
    label[strlen ( label )-1]=0;
    user_label* new = NULL;
    HASH_FIND_INT ( labels, &address, new );
    if ( new )
    {
        error_print ( "Label with the same address already declared\n" );
        //assert ( 0 );
    }
    new = malloc ( sizeof *new );
    new->address = address;
    new->label = strdup ( label );
    HASH_ADD_INT ( labels, address, new );
}

/**
 * [get_label_address  description]
 * @param  label [description]
 * @return       [description]
 */
intmax_t get_label_address ( char* label )
{
    for ( user_label* tmp=labels; tmp != NULL; tmp=tmp->hh.next )
    {
        if ( 0 == strcasecmp ( tmp->label, label ) )
        {
            return tmp->address;
        }
    }
    current_label = strdup ( label );
    return INTMAX_MIN;
}

/**
@brief Print buffer in hex representation
@param pv Buffer to print
@param len Size of the buffer
@return Nothing
*/
void hex_print ( const void* pv, size_t len )
{
    puts ( "======================START====================" );
    const uint8_t* p = ( const uint8_t* ) pv;
    if ( NULL == pv )
        puts ( "NULL" );
    else
    {
        size_t i = 0;
        size_t width = 0;
        for ( ; i < len; ++i )
        {
            if ( width++%16 == 0 )
                puts ( "" );

            printf ( "%.2X ", *p++ );
        }
    }
    puts ( "\n=======================END=====================" );
}

/**
@brief Load asm file into memory
@param filename Filename of source asm file
@param buffer Pointer to a buffer in memory (Will be allocated by the function)
@return 1 on success, 0 on error
*/
int load_file ( char* filename, char** buffer )
{
  FILE* in = fopen ( filename, "r" );
  if ( !in )
  {
      puts ( TEXT_FAILED_OPEN_FOR_READ );
      return 0;
  }
  fseek ( in, 0, SEEK_END );
  size_t fsize = ftell ( in );
  rewind ( in );

  if ( fsize > MAX_SOURCE_SIZE )
  {
      puts ( TEXT_SOURCE_FILE_TOO_LARGE );
      fclose ( in );
      return 0;
  }

  *buffer = calloc ( 1, fsize + 2 );
  if ( 0 == *buffer )
  {
      puts ( TEXT_FAILED_ALLOCATE_MEMORY );
      fclose ( in );
      return 0;
  }
  size_t rd = fread ( *buffer, fsize, 1, in );
	(void)rd;
  fclose ( in );
  strcat(*buffer, "\n");
  return 1;
}

/**
 * [print_labels description]
 * @param print [description]
 */
void print_labels ( user_label* print )
{
    user_label* cur, *tmp;
    HASH_ITER ( hh, print, cur, tmp )
    {
        printf ( "[%#.4X] \"%s\"\n", ( uint16_t ) cur->address, cur->label );
    }
}

/**
 * [cleanup  description]
 */
void cleanup ( void )
{
  FILE* lbls = fopen ( "labels.txt", "wb" );
  char txt[512];
  assert ( lbls );
  user_label* cur, *tmp;
  HASH_ITER ( hh, labels, cur, tmp )
  {
      HASH_DEL ( labels,cur );
      sprintf ( txt, "%#.4x: \"%s\"\n", ( uint16_t ) cur->address, cur->label );
      fwrite ( txt, 1, strlen ( txt ), lbls );
      free ( cur->label );
      free ( cur );
  }
  fclose ( lbls );
}

/**
 * [process_source  description]
 * @param  source [description]
 * @return        [description]
 */
int process_source ( char* source, FILE *out)
{
    asm_load_buffer ( source );
    int retval = asmparse();
    run_pass = PASS2;
    PC=0;
    asm_load_buffer ( source );
    retval = asmparse();
    fwrite(prog, 1, PC, out);
    //hex_print(prog, PC);
    return retval;
}
