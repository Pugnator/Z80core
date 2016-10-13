#include <stdarg.h>
#include <unistd.h>
#include <common.h>
#include <assembler.h>
#include <dassembler.h>
#include <inttypes.h>

void
usage (void)
{
	const char *help = "\
USAGE:\
";
}

int
main (int argc, char** argv)
{
	int opt = 0;
	int option_index = 0;
	char *source = NULL;
	char *target = NULL;
	int verbose = 0;

	struct option long_options[] =
		{
			{ "verbose", no_argument, &verbose, 0 },
			{ "source", required_argument, 0, 's' },
			{ "target", required_argument, 0, 't' },
			{ NULL, 0, 0, 0 } };

	while ((opt = getopt_long (argc, argv, "hs:t:v", long_options, &option_index))
	    != -1)
		{
			switch (opt)
				{
				case 'v':
					verbose = 1;
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
	if (!source || !target)
		{
			return 1;
		}
	read_source (source, target);
}
