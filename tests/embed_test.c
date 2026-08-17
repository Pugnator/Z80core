/**
 * @file   embed_test.c
 * @brief  Uses the assembler and disassembler the way an embedder would
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
 *
 * This file is part of Z80core, released under the terms of the GNU General
 * Public License version 2. See LICENSE.md for the full text.
 *
 * Everything here goes through the public headers only - zasm.h and zdasm.h -
 * so if this compiles and passes, the libraries are usable from outside the
 * project. It is also the worked example docs/EMBEDDING.md refers to.
 */

#include "zasm.h"
#include "zdasm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition, ...)                                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            ++failures;                                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
        }                                                                                                              \
    } while (0)

/** Somewhere for diagnostics to land, so nothing reaches stdout. */
typedef struct
{
    int count;
    int first_line;
    char first[256];
} diag_log;

static void collect(void *user, int line, const char *message)
{
    diag_log *log = user;
    if (0 == log->count)
    {
        log->first_line = line;
        snprintf(log->first, sizeof log->first, "%s", message);
    }
    ++log->count;
}

static void test_assembles_to_expected_bytes(void)
{
    static const char source[] = ".org 0\n"
                                 "start:\n"
                                 "    LD A, 0x55\n"
                                 "    LD HL, 0x8000\n"
                                 "    LD [HL], A\n"
                                 "    JR start\n"
                                 ".end\n";
    static const uint8_t expected[] = {0x3E, 0x55, 0x21, 0x00, 0x80, 0x77, 0x18, 0xF8};

    diag_log log = {0};
    zasm_image image = {0};

    const bool ok = zasm_assemble(source, ZASM_FORMAT_BIN, &image, collect, &log);

    CHECK(ok, "a valid program failed to assemble: %s", log.first);
    CHECK(0 == log.count, "a valid program produced %d diagnostic(s): %s", log.count, log.first);
    CHECK(image.size == sizeof expected, "expected %zu bytes, got %zu", sizeof expected, image.size);
    CHECK(image.bytes && 0 == memcmp(image.bytes, expected, sizeof expected), "the bytes are not what was expected");
    CHECK(0 == image.origin, "origin should be 0, is %04X", image.origin);

    zasm_image_free(&image);
    CHECK(NULL == image.bytes, "freeing the image did not clear it");
}

/** A host wants the error, its line, and no output on its terminal. */
static void test_errors_arrive_through_the_callback(void)
{
    static const char source[] = ".org 0\n"
                                 "    LD A, missing_label\n"
                                 ".end\n";

    diag_log log = {0};
    zasm_image image = {0};

    const bool ok = zasm_assemble(source, ZASM_FORMAT_BIN, &image, collect, &log);

    CHECK(!ok, "an undefined label assembled without complaint");
    CHECK(log.count > 0, "the failure produced no diagnostic");
    CHECK(2 == log.first_line, "the diagnostic points at line %d, expected 2", log.first_line);
    CHECK(NULL == image.bytes, "a failed assembly still handed back bytes");

    zasm_image_free(&image);
}

/**
 * Every line-ending style must assemble to the same bytes: LF, CRLF, LFCR and
 * classic-Mac bare CR. The endings appear mid-source and around a comment, the
 * comment being the risky spot - a rule that reads to "end of line" has to
 * know a carriage return ends one too.
 */
static void test_line_endings(void)
{
    static const struct
    {
        const char *name;
        const char *source;
    } cases[] = {
        {"LF", ".org 0\n LD A, 0x55 ; comment\n NOP\n.end\n"},
        {"CRLF", ".org 0\r\n LD A, 0x55 ; comment\r\n NOP\r\n.end\r\n"},
        {"LFCR", ".org 0\n\r LD A, 0x55 ; comment\n\r NOP\n\r.end\n\r"},
        {"CR", ".org 0\r LD A, 0x55 ; comment\r NOP\r.end\r"},
    };
    static const uint8_t expected[] = {0x3E, 0x55, 0x00};

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i)
    {
        diag_log log = {0};
        zasm_image image = {0};

        const bool ok = zasm_assemble(cases[i].source, ZASM_FORMAT_BIN, &image, collect, &log);

        CHECK(ok, "%s endings failed to assemble: %s", cases[i].name, log.first);
        CHECK(0 == log.count, "%s endings produced '%s'", cases[i].name, log.first);
        CHECK(sizeof expected == image.size, "%s endings produced %zu bytes, expected %zu", cases[i].name, image.size,
              sizeof expected);
        CHECK(image.bytes && 0 == memcmp(image.bytes, expected, sizeof expected),
              "%s endings assembled to different bytes", cases[i].name);

        zasm_image_free(&image);
    }
}

/** Diagnostics must count lines the same whichever style ends them. */
static void test_line_numbers_survive_the_ending_style(void)
{
    static const struct
    {
        const char *name;
        const char *source;
    } cases[] = {
        {"LF", ".org 0\n NOP\n LD A, missing\n.end\n"},
        {"CRLF", ".org 0\r\n NOP\r\n LD A, missing\r\n.end\r\n"},
        {"LFCR", ".org 0\n\r NOP\n\r LD A, missing\n\r.end\n\r"},
        {"CR", ".org 0\r NOP\r LD A, missing\r.end\r"},
    };

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i)
    {
        diag_log log = {0};
        zasm_image image = {0};

        const bool ok = zasm_assemble(cases[i].source, ZASM_FORMAT_BIN, &image, collect, &log);

        CHECK(!ok, "%s endings: an undefined label assembled", cases[i].name);
        CHECK(3 == log.first_line, "%s endings: diagnostic points at line %d, expected 3", cases[i].name,
              log.first_line);

        zasm_image_free(&image);
    }
}

/** State is global, so a second call must not inherit the first one's labels. */
static void test_repeated_assembly_is_independent(void)
{
    static const char source[] = ".org 0\n"
                                 "loop:\n"
                                 "    NOP\n"
                                 "    JR loop\n"
                                 ".end\n";

    for (int attempt = 0; attempt < 3; ++attempt)
    {
        diag_log log = {0};
        zasm_image image = {0};

        const bool ok = zasm_assemble(source, ZASM_FORMAT_BIN, &image, collect, &log);

        CHECK(ok, "attempt %d failed: %s", attempt, log.first);
        CHECK(0 == log.count, "attempt %d produced '%s'", attempt, log.first);
        CHECK(3 == image.size, "attempt %d produced %zu bytes, expected 3", attempt, image.size);

        zasm_image_free(&image);
    }
}

/** What the assembler emitted, the disassembler must read back. */
static void test_round_trip_through_both_libraries(void)
{
    static const char source[] = ".org 0\n"
                                 "    LD A, 0x55\n"
                                 "    JR 0\n"
                                 ".end\n";

    zasm_image image = {0};
    const bool ok = zasm_assemble(source, ZASM_FORMAT_BIN, &image, NULL, NULL);
    CHECK(ok, "the source did not assemble");
    if (!ok)
    {
        return;
    }

    zdasm_insn insn;
    uint8_t used = zdasm_decode(image.bytes, image.size, 0, &insn);
    CHECK(2 == used, "LD A,n should be 2 bytes, decoded %u", used);
    CHECK(insn.known, "LD A,n was not recognised");
    CHECK(NULL != strstr(insn.text, "LD A"), "unexpected text: %s", insn.text);

    used = zdasm_decode(image.bytes, image.size, 2, &insn);
    CHECK(2 == used, "JR should be 2 bytes, decoded %u", used);
    CHECK(insn.branches, "JR was not reported as a branch");
    CHECK(0 == insn.target, "JR should target 0000, says %04X", insn.target);

    zasm_image_free(&image);
}

/** An unknown byte still has to advance, or a caller walking a buffer stalls. */
static void test_undecodable_bytes_still_advance(void)
{
    static const uint8_t rubbish[] = {0xED, 0xFF, 0x00};

    zdasm_insn insn;
    const uint8_t used = zdasm_decode(rubbish, sizeof rubbish, 0, &insn);

    CHECK(used > 0, "an undecodable byte consumed nothing");
    CHECK(!insn.known, "rubbish was reported as a known instruction");
    CHECK(NULL != strstr(insn.text, "defb"), "expected a defb, got: %s", insn.text);
}

static void test_other_formats(void)
{
    static const char source[] = ".org 0x8000\n    NOP\n.end\n";

    zasm_image ihex = {0};
    CHECK(zasm_assemble(source, ZASM_FORMAT_IHEX, &ihex, NULL, NULL), "ihex assembly failed");
    CHECK(ihex.size > 0 && ihex.bytes && ':' == ihex.bytes[0], "ihex output does not look like Intel HEX");
    CHECK(0x8000 == ihex.origin, "origin should be 8000, is %04X", ihex.origin);
    zasm_image_free(&ihex);

    zasm_image tap = {0};
    CHECK(zasm_assemble(source, ZASM_FORMAT_TAP, &tap, NULL, NULL), "tap assembly failed");
    CHECK(tap.size > 0, "tap output is empty");
    zasm_image_free(&tap);
}

int main(void)
{
    printf("zasm %s\n", zasm_version());

    test_assembles_to_expected_bytes();
    test_errors_arrive_through_the_callback();
    test_line_endings();
    test_line_numbers_survive_the_ending_style();
    test_repeated_assembly_is_independent();
    test_round_trip_through_both_libraries();
    test_undecodable_bytes_still_advance();
    test_other_formats();

    if (0 == failures)
    {
        printf("embedding API: all checks passed\n");
        return EXIT_SUCCESS;
    }
    printf("embedding API: %d check(s) failed\n", failures);
    return EXIT_FAILURE;
}
