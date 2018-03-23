#include <stdio.h>
#include <tap.h>

#define TOK_NUMPREFIX 	0x0E
#define TOK_ENDLINE   	0x0D
#define TOK_CODE      	0xAF
#define TOK_USR       	0xC0
#define TOK_LOAD      	0xEF
#define TOK_POKE      	0xF4
#define TOK_RANDOMIZE 	0xF9
#define TOK_CLEAR     	0xFD

#define HEADER_BUFFER_SIZE (0x100)

#define LINE_HDR_SIZE	        (4)

#define BLOCK_HDR_SIZE        (18)
#define DATA_HDR_OFFSET       (3)

#define BLOCK_TYPE_PROGRAM 		(0)
#define BLOCK_TYPE_NUM_ARRAY 	(1)
#define BLOCK_TYPE_CHAR_ARRAY	(2)
#define BLOCK_TYPE_CODE				(3)
#define BLOCK_FLAG_HEADER			(0x00)
#define BLOCK_FLAG_REG			  (0x0A)
#define BLOCK_FLAG_DATA			  (0xFF)
#define BLOCK_NAME_SIZE				(10)

struct t_tap_header
{
  uint8_t type;
  char *filename;
  uint16_t data_size;
  uint16_t param1;
  uint16_t param2;
};

static int write_zx_u16_le( uint8_t p[], uint16_t val )
{
	p[0] = val&0xff;
	p[1] = (val>>8)&0xff;
  return 2;
}

static int write_zx_u16_be( uint8_t p[], uint16_t val )
{
	p[0] = (val>>8)&0xff;
	p[1] = val&0xff;
  return 2;
}

static int write_zx_number( uint8_t p[], uint16_t val )
{
	int n = 0;

	n = sprintf( (void*)p, "%d", val );
	p[n++] = TOK_NUMPREFIX;
	p[n++] = '\0';
	p[n++] = '\0';
  n += write_zx_u16_le( &p[n], val );
	p[n++] = '\0';

	return n;
}

/* create basic loader program code */
static int create_zx_basic_loader( struct t_tap_info *p_tap, uint8_t p[] )
{
	int n = 0, l, size;
  
	l = n;
	n+=LINE_HDR_SIZE;
	/* 10 CLEAR prog_start-1 */
	p[n++] = TOK_CLEAR;
	n += write_zx_number( &p[n], p_tap->prog_start - 1 );
	p[n++] = TOK_ENDLINE;
	size = n-LINE_HDR_SIZE-l;
	l += write_zx_u16_be( &p[l], 10 );
  l += write_zx_u16_le( &p[l], size );
	
	l = n;
	n += LINE_HDR_SIZE;
	/* 20 POKE 23610, 255 */
	p[n++] = TOK_POKE;
	n += write_zx_number( &p[n], 23610 );
	p[n++] = ',';
	n += write_zx_number( &p[n], 255 );
	p[n++] = TOK_ENDLINE;
	size = n-LINE_HDR_SIZE-l;
	l += write_zx_u16_be( &p[l], 20 );
  l += write_zx_u16_le( &p[l], size );

	l = n;
	n += LINE_HDR_SIZE;
	/* 30 LOAD "" CODE */
	p[n++] = TOK_LOAD;
	p[n++] = '\"';
	p[n++] = '\"';
	p[n++] = TOK_CODE;
	p[n++] = TOK_ENDLINE;
	size = n-LINE_HDR_SIZE-l;
	l += write_zx_u16_be( &p[l], 30 );
  l += write_zx_u16_le( &p[l], size );

  l = n;
  n += LINE_HDR_SIZE;
  /* 40 RANDOMIZE USR oep */
  p[n++] = TOK_RANDOMIZE;
  p[n++] = TOK_USR;
  n += write_zx_number( &p[n], p_tap->entry_point );
  p[n++] = TOK_ENDLINE;
  size = n-LINE_HDR_SIZE-l;
	l += write_zx_u16_be( &p[l], 40 );
  l += write_zx_u16_le( &p[l], size );

	return n;
}

static int tap_write_header( uint8_t p[], struct t_tap_header *p_hdr )
{
  int n = 0, i;
  uint8_t xor8;
  
  /* header size + xor8 */
  n += write_zx_u16_le( &p[n], BLOCK_HDR_SIZE+1 );
	/* header flag */
	p[n++] = BLOCK_FLAG_HEADER;
  p[n++] = p_hdr->type;

  char *s = p_hdr->filename;
  i = BLOCK_NAME_SIZE;
  while( i-- )
	{
    char ch = ' ';
    if(*s)
    {
      ch = *s;
      ++s;
    }
    p[n++]=ch;
	}
  n += write_zx_u16_le( &p[n], p_hdr->data_size );
  n += write_zx_u16_le( &p[n], p_hdr->param1 );
  n += write_zx_u16_le( &p[n], p_hdr->param2 );

  xor8 = 0;
  for( i = 0; i < BLOCK_HDR_SIZE; ++i )
  {
    xor8 ^= p[2+i]; 
  }
  p[n++] = xor8;
  return n;
}

static uint8_t tap_write_data_header( uint8_t p[], uint8_t data[], uint16_t data_size )
{
  int i;
  uint8_t xor8;

  /* include flag + xor8*/
  uint16_t block_size = data_size + 2;
	(void)write_zx_u16_le( p, block_size );
	/* header flag */
	p[2] = BLOCK_FLAG_DATA;
	
  xor8 = p[2];
  if( data )
  {
    for( i = 0; i < data_size; ++i )
    {
      xor8 ^= data[i]; 
    }
  }
  return xor8;
}


int tap_create( struct t_tap_info *p_tap, FILE *out )
{
	int l = 0;
	int data_size;
  uint8_t p[HEADER_BUFFER_SIZE] = { 0 }, xor8;
  struct t_tap_header hdr;
	

	data_size = create_zx_basic_loader( p_tap, p );
  hdr.type = BLOCK_TYPE_PROGRAM;
  hdr.filename = "loader";
  /* include xor8 */
  hdr.data_size = data_size;
  /* basic code, autostart line */
  hdr.param1 = 10;
  /* relative varible area address */
  hdr.param2 = data_size;

  l += tap_write_header( &p[l], &hdr );
  /* create actual data payload */
  (void)create_zx_basic_loader( p_tap, &p[l+DATA_HDR_OFFSET] );

  xor8 = tap_write_data_header( &p[l], &p[l+DATA_HDR_OFFSET], data_size );
  l += DATA_HDR_OFFSET + data_size;
  p[l++] = xor8;
  
  hdr.type = BLOCK_TYPE_CODE;
  hdr.filename = "rombinary";
  hdr.data_size = p_tap->rom_size;
  hdr.param1 = p_tap->entry_point;
  hdr.param2 = 32768u;
  l += tap_write_header( &p[l], &hdr );
  
  xor8 = tap_write_data_header( &p[l], p_tap->rom, p_tap->rom_size );
  l += DATA_HDR_OFFSET;
 
  /* write all headers to file */
  if( fwrite( p, 1, l, out ) != l )
    return -1;
  
  /* write actual rom data */
  if( p_tap->rom )
  {
    if( fwrite( p_tap->rom, 1, p_tap->rom_size, out ) != p_tap->rom_size )
      return -1;
  }
  /* write xor8 checksum */
  if( fwrite( &xor8, 1, 1, out ) != 1 )
    return -1;
  fflush( out );
  /* return actual tap file size */
	return l+p_tap->rom_size+1;	
}
