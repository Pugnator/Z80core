#include <stdarg.h>
#include <unistd.h>
#include <assembler.h>
#include <dassembler.h>
#include <getopt.h>
#include <inttypes.h>

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

void assembly_listing (char *filename, char *output_filename)
{
 	FILE *source, *target;
  char *source_listing;
  size_t read, sz;
	puts("Z80 assembler");
  
  	source = fopen (filename, "rb");
	if(!source)
	{
		puts("Failed to open source file");
		return;
	}
  	/* get file size */
	fseek(source, 0L, SEEK_END);
  	sz = ftell(source);
	rewind(source);
  	/* file size + null terminator */
  	source_listing = malloc(sz+1);
  	assert( source_listing );
  	read = fread( source_listing, 1, sz, source);
  	fclose( source );
  	if( read != sz )
  	{
		puts("Failed to read input file");
		return;
  	}
  	/* make null terminated string */
	source_listing[sz] = '\0';
  	target = fopen (output_filename, "wb");
	if(!target)
	{
		free(source_listing);
		puts("Failed to open output file");
		return;
	}
	process_source(source_listing, target);
	free(source_listing);
	fclose(target);
}

int main (int argc, char** argv)
{
	int opt = 0;
	char *source = NULL;
	char *target = NULL;
	int verbose = 0;
  int test = 0;
	int dasm = 0;

#ifdef __GNUC__
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

	if(dasm && source)
	{
		disassembly_listing(source);
	}

	if (!source || !target)
	{
		return 1;
	}
	
	assembly_listing (source, target);
}
