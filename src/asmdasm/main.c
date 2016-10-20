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
}

void assembly_listing (char *filename, char *output_filename)
{
	puts("Z80 assembler by Lavrenty Ivanov, 2014");
	FILE *source = fopen (filename, "r");
	assert(source);
	char source_listing[65535];
	char buf[1024];
	size_t read=0;
	size_t total_size = 0;
	while((read = fread(buf, 1, sizeof buf, source))>0)
	{
		memcpy(source_listing + total_size, buf, read);
		total_size+=read;
	}
	fclose(source);

	FILE *target = fopen (output_filename, "w");
	assert(target);
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
