/**
 *
 * @file   main.c 
 * @date   16.03.2018 
 * @license This project is released under the GPL 2 license.
 * @brief main file
 *
 */

#include <stdarg.h>
#include <unistd.h>
#include <assembler.h>
#include <dassembler.h>
#include <getopt.h>
#include <inttypes.h>

int verbose = 0;

void usage (void)
{
	const char *help = 
"USAGE:\r\n"
"[-s | --source] source filename\r\n"
"[-t | --target] output filename\r\n"
"[-d | --disassemble] filename of the binary to disassemble\r\n"
"[-v | --verbose] verbose output"
;
	puts(help);
}

bool assembly_listing (char *filename, char *output_filename)
{
 	FILE *source, *target;
  char *source_listing;
  size_t read, sz;
	puts("Z80 assembler");
  
  source = fopen (filename, "rb");
	if(!source)
	{
		puts("Failed to open source file");
		return false;
	}
  	/* get file size */
	fseek(source, 0L, SEEK_END);
  sz = ftell(source);
	rewind(source);
  /* file size + null terminator */
  source_listing = malloc(sz + 2);
  assert( source_listing );
  read = fread( source_listing, 1, sz, source);
  fclose( source );
  if( read != sz )
  {
    puts("Failed to read input file");
    return false;
  }
  /* make null terminated string */
	source_listing[sz] = '\n';
  source_listing[sz + 1] = '\0';  
  target = fopen (output_filename, "wb");
	if(!target)
	{
		free(source_listing);
		puts("Failed to open output file");
		return false;
	}
	int result = process_source(source_listing, target);
	free(source_listing);
	fclose(target);
  return ASM_OK == result;
}

int main (int argc, char** argv)
{
	int opt = 0;
	char *source = NULL;
	char *target = NULL;	
  int test = 0;
	int dasm = 0;

#if defined(__GNUC__) || defined(__MINGW32__)
	int option_index = 0;
	struct option long_options[] =
		{
			{ "verbose", no_argument, &verbose, 0 },
			{ "disassemble", no_argument, &dasm, 0 },
			{ "source", required_argument, 0, 's' },
			{ "output", required_argument, 0, 'o' },
      { "test", no_argument, &test, 't' },
			{ NULL, 0, 0, 0 } 
		};
	while ((opt = getopt_long (argc, argv, "hs:o:tdv", long_options, &option_index))!= -1)
#else
	while ((opt = _getopt(argc, argv, "hs:o:tdv"))!= -1)
#endif  
	{
		switch (opt)
			{
			case 'v':
				verbose = 1;
				break;
			case 'd':
				dasm = 1;
				break;
			case 's':
				source = optarg;
				break;
      case 'o':
        target = optarg;
        break;
			case 't':
        puts("Not implemented yet");
				return 0;
			case 'h':
			case '?':
				usage ();
				return 0;
				break;
			}
	}
  
  /* Check for any non option params here */
  if (optind < argc)
  {
    source = argv[optind];
    /*TODO: asm include
    while (optind < argc)
    {
      //argv[optind++];
    }
    */
  }

	if(dasm && source)
	{
		disassembly_listing(source);
	}

	if (!source || !target)
	{
		return 1;
	}
	
	return assembly_listing (source, target);
}
