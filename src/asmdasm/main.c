#include <stdarg.h>
#include <unistd.h>
#include <assembler.h>
#include <dassembler.h>
#include <inttypes.h>

void usage (void)
{
	const char *help = "\
USAGE:\
";
	puts(help);
}

void assembly_listing (char *filename, char *output_filename)
{
	puts("Z80 assembler by Lavrenty Ivanov, 2016");
	FILE *source = fopen (filename, "rt");
	if(!source)
	{
		puts("Failed to open source file");
		return;
	}
	FILE *target = fopen (output_filename, "wb");
	if(!target)
	{
		puts("Failed to open output file");
		return;
	}

	fseek(source, 0L, SEEK_END);
	size_t sz = ftell(source);
	rewind(source);
	char *source_listing = malloc(sz);
	char buf[1024] = {};
	size_t read=0;
	size_t total = 0;
	while((read = fread(buf, 1, sizeof buf, source))>0)
	{
		memcpy(source_listing + total, buf, read);
		total+=read;
	}
	fclose(source);
	process_source(source_listing, target);
	free(source_listing);
	fclose(target);
}

int main (int argc, char** argv)
{
	int opt = 0;
	int option_index = 0;
	char *source = NULL;
	char *target = NULL;
	int verbose = 0;
	int dasm = 0;

	struct option long_options[] =
		{
			{ "verbose", no_argument, &verbose, 0 },
			{ "disassemble", no_argument, &dasm, 0 },
			{ "source", required_argument, 0, 's' },
			{ "target", required_argument, 0, 't' },
			{ NULL, 0, 0, 0 } };

	while ((opt = getopt_long (argc, argv, "hs:t:dv", long_options, &option_index))
	    != -1)
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
			case 't':
				target = optarg;
				break;
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
