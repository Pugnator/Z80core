#include <ctype.h>
#include <string.h>
#include "utils.h"

static const char ws[] = "\r\n\t ";
char *rtrim(char *s)
{
  int len;
  len = strlen(s);
  while (len--)
  {
    if (!strchr(ws, s[len]))
      break;
    s[len] = '\0';
  }

  return s;
}

char *ltrim(char *s)
{
  int i = 0;
  while (s[i])
  {
    if (!strchr(ws, s[i]))
      break;
    ++i;
  }

  return &s[i];
}

int isxstring(char *s)
{
  while (*s)
  {
    if (!isxdigit(*s))
      return 0;
    ++s;
  }
  return 1;
}

unsigned char chx2v(char a)
{
  return (a > '9') ? (uint8_t) ((a & (~0x20)) - 0x37) : (uint8_t) (a - '0');
}

unsigned char sx2v(char *s)
{
  return (chx2v(s[0]) << 4) | chx2v(s[1]);
}

/* return new len */
int shrink_xs2bin(char *io)
{
  int len;
  len = strlen(io);

  if (len & 1)
    return -1;

  for (int i = 0; i < len; i += 2)
  {
    io[i / 2] = sx2v(io + i);
  }
  return len / 2;
}

/* data pack, unpack */
uint16_t u16_be(uint8_t *b)
{
  return b[0] << 8 | b[1];
}

void u16_be_pack(uint8_t *b, uint16_t val)
{
  b[0] = (val >> 8);
  b[1] = (val & 0xFF);
}

uint32_t u32_be(uint8_t *b)
{
  return b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3];
}

uint32_t u24_be(uint8_t *b)
{
  return b[0] << 16 | b[1] << 8 | b[2];
}

uint16_t u16_le(uint8_t *b)
{
  return b[1] << 8 | b[0];
}

uint32_t u32_le(uint8_t *b)
{
  return b[3] << 24 | b[2] << 16 | b[1] << 8 | b[0];
}

uint32_t u24_le(uint8_t *b)
{
  return b[2] << 16 | b[1] << 8 | b[0];
}
