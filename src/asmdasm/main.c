/**
 * @file   main.c
 * @brief  Command line entry point
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
 *
 * This file is part of Z80core, released under the terms of the GNU General
 * Public License version 2. See LICENSE.md for the full text.
 */

#include <stdarg.h>
#include <unistd.h>
#include <assembler.h>
#include <dassembler.h>
#include <getopt.h>
#include <inttypes.h>

void usage(void)
{
    const char *help = "USAGE:\r\n"
                       "[-s | --source] source filename\r\n"
                       "[-o | --output] output filename\r\n"
                       "[-t | --test]\r\n"
                       "[-d | --disassemble] disassemble the source file instead of assembling\r\n"
                       "[-x | --format] output file format: bin (default), tap, ihex\r\n"
                       "[-v | --verbose] verbose output\r\n"
                       "[-r | --report] print completion summary\r\n"
                       "[-h | --help] print this help";
    puts(help);
}

bool assembly_listing(char *filename, char *output_filename, char *output_format)
{
    FILE *source, *target;
    char *source_listing;
    size_t read, sz;

    source = fopen(filename, "rb");
    if (!source)
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
    assert(source_listing);
    read = fread(source_listing, 1, sz, source);
    fclose(source);
    if (read != sz)
    {
        puts("Failed to read input file");
        free(source_listing);
        return false;
    }
    /* make null terminated string */
    source_listing[sz] = '\n';
    source_listing[sz + 1] = '\0';
    target = fopen(output_filename, "wb");
    if (!target)
    {
        free(source_listing);
        puts("Failed to open output file");
        return false;
    }
    int result = process_source(source_listing, output_format, target);
    free(source_listing);
    fclose(target);
    return ASM_OK == result;
}

int main(int argc, char **argv)
{
    int opt = 0;
    char *source = NULL;
    char *target = NULL;
    char *output_fmt = "bin";
    int dasm = 0;
    int analyse = 0;
    int report = 0;

#if defined(__GNUC__) || defined(__MINGW32__)
    int option_index = 0;
    int test = 0;
    struct option long_options[] = {{"verbose", no_argument, &verbose, 1},
                                    {"report", no_argument, &report, 1},
                                    {"disassemble", no_argument, &dasm, 1},
                                    {"analyse", no_argument, &analyse, 1},
                                    {"source", required_argument, 0, 's'},
                                    {"output", required_argument, 0, 'o'},
                                    {"format", required_argument, 0, 'x'},
                                    {"test", no_argument, &test, 1},
                                    {"help", no_argument, 0, 'h'},
                                    {NULL, 0, 0, 0}};
    while ((opt = getopt_long(argc, argv, "x:hs:o:tdvra", long_options, &option_index)) != -1)
#else
    while ((opt = _getopt(argc, argv, "x:hs:o:tdvra")) != -1)
#endif
    {
        switch (opt)
        {
        case 'x':
            output_fmt = optarg;
            if (output_fmt && 0 == strcmp(output_fmt, "format"))
            {
                if (optind < argc)
                {
                    output_fmt = argv[optind++];
                }
                else
                {
                    puts("Missing argument for -xformat option");
                    return 1;
                }
            }
            break;
        case 'v':
            verbose = 1;
            break;
        case 'r':
            report = 1;
            break;
        case 'd':
            dasm = 1;
            break;
        case 'a':
            analyse = 1;
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
            usage();
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

    if (!source)
    {
        puts("No source file given");
        usage();
        return EXIT_FAILURE;
    }

    if (dasm)
    {
        return disassembly_listing(source, 0 != analyse) ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (!target)
    {
        puts("No output file given");
        usage();
        return EXIT_FAILURE;
    }

    if (!assembly_listing(source, target, output_fmt))
    {
        return EXIT_FAILURE;
    }
    if (report)
    {
        printf("Assembled %zu bytes to %s (%s)\n", assembled_bytes, target, output_fmt);
    }

    return EXIT_SUCCESS;
}
