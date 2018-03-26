#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <utils.h>
#include <ihex.h>

#define IHEX_BINARY_DATA      (0)
#define IHEX_EOF              (1)
#define IHEX_SEGMENT          (2)
#define IHEX_START_SEG_RECORD (3)
#define IHEX_EX_ADDR          (4)
#define IHEX_LIN_ADDR         (5)
#define HEADER_SIZE           (4)
#define BLOCK_SIZE            (16)

int save_array_to_ihex(FILE *ihex, int base, uint8_t b[], int size)
{
  char buf[(HEADER_SIZE + BLOCK_SIZE + 4) * 2];
  int len, chunk_size;
  uint8_t sum8, data;

  assert(BLOCK_SIZE <= 255);
  for (int i = size; i > 0; i -= chunk_size, base += chunk_size)
  {
    chunk_size = i > BLOCK_SIZE ? BLOCK_SIZE : i;

    sum8 = chunk_size + ((base >> 8) & 0xff) + (base & 0xff) + IHEX_BINARY_DATA;
    /* write header */
    len = sprintf(buf, ":%.2X%.4X%.2X", chunk_size & 0xFF, base & 0xFFFF, IHEX_BINARY_DATA);
    if (fwrite(buf, 1, len, ihex) != len)
      return -1;

    for (int n = 0; n < chunk_size; ++n)
    {
      data = *b++;
      sum8 += data;
      len = sprintf(buf, "%.2X", data);
      if (fwrite(buf, 1, len, ihex) != len)
        return -1;
    }

    len = sprintf(buf, "%.2X\r\n", (0 - sum8) & 0xFF);
    if (fwrite(buf, 1, len, ihex) != len)
      return -1;
  }
  /* write EOF marker */
  len = sprintf(buf, ":%.2X%.4X%.2X%.2X", 0, 0, IHEX_EOF, (0 - IHEX_EOF) & 0xFF);
  if (fwrite(buf, 1, len, ihex) != len)
    return -1;

  return -1;
}

void *create_array_from_ihex(FILE *ihex, int *res)
{
  char buf[0x100], *s;
  int err = 0, len;
  uint8_t *b, *mem = 0, sum8;
  uint32_t ex_addr = 0, total_size = 0, new_size;
  while (!feof(ihex))
  {
    s = fgets(buf, sizeof(buf), ihex);
    if (!s)
      continue;
    /* prepare and validate input data */
    s = ltrim(s);
    /* empty string */
    if (!s)
      continue;
    if (s[0] != ':')
    {
      err = -6;
      continue;
    }
    ++s;
    s = rtrim(s);
    if (!isxstring(s))
    {
      err = -1;
      break;
    }
    /* must be even */
    len = strlen(s);
    if (len & 1)
    {
      err = -2;
      break;
    }
    /* shrink to binary */
    len = shrink_xs2bin(s);
    if (len < 0)
    {
      err = -3;
      break;
    }
    b = (void *)s;
    /* check checksum */
    sum8 = 0;
    for (int i = 0; i < len; i++)
    {
      sum8 += b[i];
    }
    if (sum8 != 0)
    {
      err = -4;
      break;
    }
    {
      uint8_t block_size, block_type;
      uint32_t block_addr;

      block_size = b[0];
      block_addr = u16_be(b + 1);
      block_type = b[3];

      /* size+addr+type+...data..+sum8 */
      if ((1 + 2 + 1 + block_size + 1) != len)
      {
        err = -5;
        break;
      }

      switch (block_type)
      {
        case IHEX_BINARY_DATA:
          block_addr += ex_addr;
          new_size = (block_addr + block_size);
          if (total_size < new_size)
          {
            mem = realloc(mem, new_size);
            assert(mem);
            /* simulate erased state */
            memset(&mem[total_size], 0xFF, new_size - total_size);
            total_size = new_size;
          }
          memcpy(&mem[block_addr], b + HEADER_SIZE, block_size);
          break;
        case IHEX_EOF:
          err = 0;
          *res = total_size;
          return mem;
          break;
        case IHEX_SEGMENT:
          assert(block_size == 2);
          ex_addr = u16_be(b + HEADER_SIZE) << 4;
          break;
        case IHEX_START_SEG_RECORD:
          assert(0);
          break;
        case IHEX_EX_ADDR:
          assert(block_size == 2);
          ex_addr = u16_be(b + HEADER_SIZE) << 16;
          break;
        case IHEX_LIN_ADDR:
          assert(0);
          break;
      }

      if (err < 0)
        break;
    }
  }

  if (err < 0)
  {
    free(mem);
    mem = 0;
    *res = err;
  }
  else
  {
    *res = total_size;
  }
  return mem;
}
