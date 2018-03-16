/**
 *
 * @file   compat.c 
 * @date   16.03.2018 
 * @license This project is released under the GPL 2 license.
 * @brief compatibility workarounds
 *
 */
 
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

int vasprintf( char **sptr, const char *fmt, va_list argv )
{
  int wanted = vsnprintf( *sptr = NULL, 0, fmt, argv );
  if( wanted < 0 )
    return -1;
  ++wanted;
  *sptr = malloc( wanted );
  if( *sptr == 0 )
    return -1;
  return vsnprintf( *sptr, wanted, fmt, argv );
}

int asprintf( char **sptr, char *fmt, ... )
{
  int retval;
  va_list argv;
  va_start( argv, fmt );
  retval = vasprintf( sptr, fmt, argv );
  va_end( argv );
  return retval;
}
