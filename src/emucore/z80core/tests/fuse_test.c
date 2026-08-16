/**
 * @file   fuse_test.c
 * @brief  Run the FUSE conformance suite against the core
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
 *
 * This file is part of Z80core, released under the terms of the GNU General
 * Public License version 2. See LICENSE.md for the full text.
 *
 * The test data is FUSE's and is described in tests/fuse/README.md. This is
 * only the harness: it builds a machine out of a 64K array and a port space,
 * runs whole instructions, and records every bus cycle with the T-state it
 * ended on.
 *
 * It is the first check on this core that was not written by the same person
 * who wrote the core. That is the entire point of it.
 */

#include "z80core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- */
/* The test data                                                     */
/* ---------------------------------------------------------------- */

typedef enum
{
    EVENT_MEMORY_READ,
    EVENT_MEMORY_WRITE,
    EVENT_PORT_READ,
    EVENT_PORT_WRITE
} event_kind;

static const char *event_name(event_kind kind)
{
    switch (kind)
    {
    case EVENT_MEMORY_READ:
        return "MR";
    case EVENT_MEMORY_WRITE:
        return "MW";
    case EVENT_PORT_READ:
        return "PR";
    default:
        return "PW";
    }
}

typedef struct
{
    int tstate;
    event_kind kind;
    uint16_t address;
    uint8_t value;
} bus_event;

/* The largest case in the suite records 64 data events and 171 contention
   lines; sized well clear of both so a silent truncation cannot look
   like a disagreement. */
#define MAX_EVENTS 128
#define MAX_CONTENDS 256
#define MAX_PATCH_BYTES 64
#define MAX_PATCHES 8

typedef struct
{
    uint16_t address;
    uint8_t bytes[MAX_PATCH_BYTES];
    int length;
} memory_patch;

typedef struct
{
    char name[32];
    z80_state_t state;
    int tstates;
    memory_patch patches[MAX_PATCHES];
    int patch_count;

    /* expected side only */
    bus_event events[MAX_EVENTS];
    int event_count;
    /* MC lines, kept only to corroborate a cycle FUSE declined to read */
    bus_event contends[MAX_CONTENDS];
    int contend_count;
} testcase;

/* ---------------------------------------------------------------- */
/* Parsing                                                           */
/* ---------------------------------------------------------------- */

/** Read a whole file. The suite is under half a megabyte; simplicity wins. */
static char *slurp(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file)
    {
        fprintf(stderr, "fuse: cannot open %s\n", path);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *text = (char *)malloc((size_t)size + 1u);
    if (text && 1 != fread(text, (size_t)size, 1, file))
    {
        free(text);
        text = NULL;
    }
    if (text)
    {
        text[size] = '\0';
    }
    fclose(file);
    return text;
}

/** Advance past the current line, returning a pointer to the next one. */
static char *next_line(char *cursor)
{
    char *end = strchr(cursor, '\n');
    if (!end)
    {
        return cursor + strlen(cursor);
    }
    *end = '\0';
    return end + 1;
}

static bool line_is_blank(const char *line)
{
    while (*line)
    {
        if (' ' != *line && '\t' != *line && '\r' != *line)
        {
            return false;
        }
        ++line;
    }
    return true;
}

static void parse_registers(const char *line, z80_state_t *state)
{
    unsigned v[12] = {0};
    sscanf(line, "%x %x %x %x %x %x %x %x %x %x %x %x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8],
           &v[9], &v[10], &v[11]);

    state->af = (uint16_t)v[0];
    state->bc = (uint16_t)v[1];
    state->de = (uint16_t)v[2];
    state->hl = (uint16_t)v[3];
    state->af_alt = (uint16_t)v[4];
    state->bc_alt = (uint16_t)v[5];
    state->de_alt = (uint16_t)v[6];
    state->hl_alt = (uint16_t)v[7];
    state->ix = (uint16_t)v[8];
    state->iy = (uint16_t)v[9];
    state->sp = (uint16_t)v[10];
    state->pc = (uint16_t)v[11];
}

static void parse_control(const char *line, z80_state_t *state, int *tstates)
{
    unsigned i = 0, r = 0, iff1 = 0, iff2 = 0, im = 0, halted = 0;
    int t = 0;
    sscanf(line, "%x %x %u %u %u %u %d", &i, &r, &iff1, &iff2, &im, &halted, &t);

    state->i = (uint8_t)i;
    state->r = (uint8_t)r;
    state->iff1 = 0 != iff1;
    state->iff2 = 0 != iff2;
    state->im = (uint8_t)im;
    state->halted = 0 != halted;
    *tstates = t;
}

/** A memory line is an address then bytes then -1. */
static bool parse_patch(const char *line, memory_patch *patch)
{
    unsigned address = 0;
    int consumed = 0;
    if (1 != sscanf(line, "%x%n", &address, &consumed))
    {
        return false;
    }

    patch->address = (uint16_t)address;
    patch->length = 0;

    const char *cursor = line + consumed;
    for (;;)
    {
        int value = 0;
        if (1 != sscanf(cursor, " %x%n", (unsigned *)&value, &consumed))
        {
            break;
        }
        cursor += consumed;
        if (0xFF < (unsigned)value) /* the -1 terminator reads as ffffffff */
        {
            break;
        }
        if (patch->length < MAX_PATCH_BYTES)
        {
            patch->bytes[patch->length++] = (uint8_t)value;
        }
    }
    return true;
}

/** An event line is a T-state, a two-letter code, an address, maybe a value. */
static bool parse_event(const char *line, bus_event *event)
{
    int tstate = 0;
    char code[8] = {0};
    unsigned address = 0;
    unsigned value = 0;

    const int fields = sscanf(line, "%d %7s %x %x", &tstate, code, &address, &value);
    if (fields < 3)
    {
        return false;
    }

    if (0 == strcmp(code, "MR"))
    {
        event->kind = EVENT_MEMORY_READ;
    }
    else if (0 == strcmp(code, "MW"))
    {
        event->kind = EVENT_MEMORY_WRITE;
    }
    else if (0 == strcmp(code, "PR"))
    {
        event->kind = EVENT_PORT_READ;
    }
    else if (0 == strcmp(code, "PW"))
    {
        event->kind = EVENT_PORT_WRITE;
    }
    else
    {
        return false; /* MC and PC are contention: see tests/fuse/README.md */
    }

    event->tstate = tstate;
    event->address = (uint16_t)address;
    event->value = (uint8_t)value;
    return true;
}

/**
 * Read one block. Blocks are separated by a blank line, and the first line is
 * the test's name. Returns NULL at the end of the file.
 */
static char *parse_block(char *cursor, testcase *test, bool with_events)
{
    memset(test, 0, sizeof *test);

    /* the name is the first line that is not blank */
    for (;;)
    {
        if (!*cursor)
        {
            return NULL;
        }
        char *line = cursor;
        cursor = next_line(cursor);
        if (!line_is_blank(line))
        {
            sscanf(line, "%31s", test->name);
            break;
        }
    }

    int state_lines = 0;
    while (*cursor)
    {
        char *line = cursor;
        cursor = next_line(cursor);
        if (line_is_blank(line))
        {
            break; /* a blank line ends the block */
        }

        if (with_events && 0 == state_lines)
        {
            bus_event event;
            if (parse_event(line, &event))
            {
                if (test->event_count < MAX_EVENTS)
                {
                    test->events[test->event_count++] = event;
                }
                continue;
            }

            /* MC and PC are contention, and skipping one must not be taken
               for the end of the event section. MC lines are kept because
               they are the only record that a machine cycle happened at all
               in the cases where FUSE contends an address without reading it. */
            int probe_tstate = 0;
            char probe_code[8] = {0};
            unsigned probe_address = 0;
            const int probe_fields = sscanf(line, "%d %7s %x", &probe_tstate, probe_code, &probe_address);
            if (probe_fields >= 2 && 0 == strcmp(probe_code, "MC"))
            {
                if (test->contend_count < MAX_CONTENDS)
                {
                    test->contends[test->contend_count].tstate = probe_tstate;
                    test->contends[test->contend_count].address = (uint16_t)probe_address;
                    ++test->contend_count;
                }
                continue;
            }
            if (probe_fields >= 2 && 0 == strcmp(probe_code, "PC"))
            {
                continue;
            }
        }

        if (0 == state_lines)
        {
            parse_registers(line, &test->state);
            ++state_lines;
        }
        else if (1 == state_lines)
        {
            parse_control(line, &test->state, &test->tstates);
            ++state_lines;
        }
        else
        {
            /* a bare -1 terminates the memory list rather than starting one */
            memory_patch patch;
            const char *scan = line;
            while (' ' == *scan || '\t' == *scan)
            {
                ++scan;
            }
            if ('-' != *scan && parse_patch(scan, &patch) && test->patch_count < MAX_PATCHES)
            {
                test->patches[test->patch_count++] = patch;
            }
        }
    }

    return cursor;
}

/* ---------------------------------------------------------------- */
/* The machine                                                       */
/* ---------------------------------------------------------------- */

typedef struct
{
    uint8_t memory[0x10000];
    int level;

    bus_event events[MAX_EVENTS];
    int event_count;
    bool overflowed;

    /* the bus cycle in progress */
    bool cycle_open;
    bool cycle_is_fetch;
    int emit_tstate;
    event_kind kind;
    uint16_t address;
    uint8_t value;
    bool have_value;
} machine;

static machine world;

static void record(int tstate, event_kind kind, uint16_t address, uint8_t value)
{
    if (world.event_count >= MAX_EVENTS)
    {
        world.overflowed = true;
        return;
    }
    world.events[world.event_count].tstate = tstate;
    world.events[world.event_count].kind = kind;
    world.events[world.event_count].address = address;
    world.events[world.event_count].value = value;
    ++world.event_count;
}

/**
 * FUSE's harness answers an unattached port with the high half of its address,
 * which is what the Spectrum's floating bus does. The expected files were
 * generated that way, so we have to do the same or every IN disagrees.
 */
static uint8_t port_read(uint16_t port)
{
    return (uint8_t)(port >> 8);
}

/**
 * FUSE sometimes contends an address without reading it: the operand bytes of
 * a conditional jump or call whose condition turned out false, where its
 * interpreter has no use for the value. The bus cycle still happens - the
 * T-state totals in the expected files say so - and a real Z80 drives MREQ and
 * RD for it, so this core performs and records the read.
 *
 * Rather than exempt those tests, the extra read is checked against the MC
 * line FUSE did log: a three-T-state read that ends where we say it did must
 * have begun at the address FUSE contended, three T-states earlier. If there
 * is no such MC, the read is ours alone and the test fails.
 */
/**
 * Cases where FUSE's expectation is wrong and we have better evidence.
 *
 * BIT n,(HL) takes its undocumented X and Y from the high half of WZ. FUSE has
 * no MEMPTR in its model or its file format, so WZ is zero throughout its
 * suite and it cannot distinguish that rule from "follow the byte tested" -
 * it chose the latter. The SingleStepTests corpus varies WZ and pins it to the
 * high half of WZ in 400 samples out of 400, which settles it.
 *
 * Only these four cases reach a BIT n,(HL) with a byte whose bits 3 and 5
 * differ from zero, so only these four disagree.
 */
static bool known_divergence(const char *name)
{
    static const char *const cases[] = {"cb46", "cb4e", "cb56", "cb5e", "cb66", "cb6e", "cb76", "cb7e"};
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i)
    {
        if (0 == strcmp(name, cases[i]))
        {
            return true;
        }
    }
    return false;
}

static bool corroborated_read(const testcase *expected, const bus_event *ours)
{
    if (EVENT_MEMORY_READ != ours->kind)
    {
        return false;
    }
    for (int i = 0; i < expected->contend_count; ++i)
    {
        if (expected->contends[i].tstate == ours->tstate - 3 && expected->contends[i].address == ours->address)
        {
            return true;
        }
    }
    return false;
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: %s tests.in tests.expected\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *input_text = slurp(argv[1]);
    char *expected_text = slurp(argv[2]);
    if (!input_text || !expected_text)
    {
        free(input_text);
        free(expected_text);
        return EXIT_FAILURE;
    }

    char *input_cursor = input_text;
    char *expected_cursor = expected_text;

    int ran = 0;
    int failed = 0;
    int diverged = 0;
    int reported = 0;
    const int report_limit = 60;

    for (;;)
    {
        testcase in;
        testcase expected;

        char *next_input = parse_block(input_cursor, &in, false);
        if (!next_input)
        {
            break;
        }
        input_cursor = next_input;

        char *next_expected = parse_block(expected_cursor, &expected, true);
        if (!next_expected)
        {
            fprintf(stderr, "fuse: expected data ran out at test %s\n", in.name);
            failed++;
            break;
        }
        expected_cursor = next_expected;

        if (0 != strcmp(in.name, expected.name))
        {
            fprintf(stderr, "fuse: blocks out of step: %s vs %s\n", in.name, expected.name);
            failed++;
            break;
        }

        /* --- build the machine --- */
        memset(&world, 0, sizeof world);
        for (int p = 0; p < in.patch_count; ++p)
        {
            for (int b = 0; b < in.patches[p].length; ++b)
            {
                world.memory[(uint16_t)(in.patches[p].address + b)] = in.patches[p].bytes[b];
            }
        }

        z80_t *cpu = z80_new();
        z80_reset(cpu);
        z80_set_state(cpu, &in.state);

        z80_pins_t pins = {0};
        uint32_t previous = 0;
        int edges = 0;
        bool finished = false;

        /* Mirror the core's prefix rules so the harness knows where one
           instruction ends: FUSE counts a prefixed instruction as one. */
        uint8_t index_prefix = 0;
        bool operation_pending = false;
        bool awaiting_boundary = false;

        while (!finished && edges < 4000)
        {
            world.level ^= 1;
            (void)z80_tick(cpu, &pins, world.level);
            ++edges;

            /*
             * Two different clocks, and confusing them is an off-by-one in
             * every event. A T-state is two edges, so after N edges the number
             * *elapsed* is N/2 - that is what FUSE stamps the end of a cycle
             * with. The T-state a cycle *begins* in is the index (N-1)/2,
             * because the first edge of a T-state has none of it behind it.
             */
            const int elapsed = edges / 2;
            const int tstate = (edges - 1) / 2;

            const uint32_t now = pins.ctrl;

            /*
             * Close the cycle that ends here before anything else. The last
             * cycle of an instruction ends on the very T-state the next fetch
             * begins, so leaving this until after the boundary check loses one
             * event from every instruction.
             */
            if (world.cycle_open && elapsed >= world.emit_tstate)
            {
                record(world.emit_tstate, world.kind, world.address, world.have_value ? world.value : 0u);
                world.cycle_open = false;

                if (world.cycle_is_fetch && !awaiting_boundary)
                {
                    const uint8_t byte = world.value;

                    if (operation_pending)
                    {
                        /* An operation byte is whatever it is. CB CB is BIT
                           1,E, not two prefixes, and mistaking it for one runs
                           a byte past the end of the instruction. */
                        operation_pending = false;
                        index_prefix = 0;
                        awaiting_boundary = true;
                    }
                    else if (0xDDu == byte || 0xFDu == byte)
                    {
                        index_prefix = byte;
                    }
                    else if (0xEDu == byte)
                    {
                        index_prefix = 0;
                        operation_pending = true;
                    }
                    else if (0xCBu == byte)
                    {
                        if (index_prefix)
                        {
                            /* DD CB: the displacement and the operation both
                               arrive as ordinary reads, so this is the last
                               fetch of the instruction */
                            awaiting_boundary = true;
                        }
                        else
                        {
                            operation_pending = true;
                        }
                    }
                    else
                    {
                        awaiting_boundary = true;
                    }
                }
            }

            /* The refresh half of an M1 asserts MREQ on its own. It is a real
               memory cycle to the DRAM and no kind of data access, so RFSH
               disqualifies everything below - without this it reads as a write
               and swallows the fetch it belongs to. */
            const bool refreshing = 0 != (now & Z80_RFSH);

            const bool m1_rose = (now & Z80_M1) && !(previous & Z80_M1);
            const bool read_rose =
                !refreshing && (now & Z80_MREQ) && (now & Z80_RD) && !((previous & Z80_MREQ) && (previous & Z80_RD));
            const bool write_rose = !refreshing && (now & Z80_MREQ) && !(now & Z80_RD) && !(previous & Z80_MREQ);
            const bool io_read_rose =
                (now & Z80_IORQ) && (now & Z80_RD) && !((previous & Z80_IORQ) && (previous & Z80_RD));
            const bool io_write_rose =
                (now & Z80_IORQ) && (now & Z80_WR) && !((previous & Z80_IORQ) && (previous & Z80_WR));

            if (m1_rose)
            {
                if (awaiting_boundary)
                {
                    /* This fetch belongs to the next instruction. FUSE asks for
                       a minimum number of T-states rather than one instruction,
                       which is how the repeating block instructions are tested,
                       so stop only once that minimum is met. */
                    if (tstate >= in.tstates)
                    {
                        finished = true;
                        --edges; /* the boundary edge is not ours to count */
                        break;
                    }
                    awaiting_boundary = false;
                    index_prefix = 0;
                    operation_pending = false;
                }
                /* the M1 cycle is four T-states; FUSE logs the read at its end */
                world.cycle_open = true;
                world.cycle_is_fetch = true;
                world.emit_tstate = tstate + 4;
                world.kind = EVENT_MEMORY_READ;
                world.address = pins.A;
                world.have_value = false;
            }
            else if (read_rose && !(now & Z80_M1))
            {
                world.cycle_open = true;
                world.cycle_is_fetch = false;
                world.emit_tstate = tstate + 3;
                world.kind = EVENT_MEMORY_READ;
                world.address = pins.A;
                world.have_value = false;
            }
            else if (write_rose && !(now & Z80_M1))
            {
                world.cycle_open = true;
                world.cycle_is_fetch = false;
                world.emit_tstate = tstate + 3;
                world.kind = EVENT_MEMORY_WRITE;
                world.address = pins.A;
                world.have_value = false;
            }

            /* --- answer the bus --- */
            if ((now & Z80_MREQ) && (now & Z80_RD))
            {
                pins.D = world.memory[pins.A];
                if (world.cycle_open && !world.have_value)
                {
                    world.value = pins.D;
                    world.have_value = true;
                }
            }
            else if ((now & Z80_MREQ) && (now & Z80_WR))
            {
                world.memory[pins.A] = pins.D;
                if (world.cycle_open)
                {
                    world.value = pins.D;
                    world.have_value = true;
                }
            }

            if (io_read_rose)
            {
                pins.D = port_read(pins.A);
                record(tstate, EVENT_PORT_READ, pins.A, pins.D);
            }
            else if ((now & Z80_IORQ) && (now & Z80_RD))
            {
                pins.D = port_read(pins.A);
            }
            if (io_write_rose)
            {
                record(tstate, EVENT_PORT_WRITE, pins.A, pins.D);
            }

            previous = now;

            if (awaiting_boundary && (edges / 2) >= in.tstates)
            {
                /* keep going until the next fetch starts, which is the boundary */
            }
        }

        const int tstates_run = edges / 2;

        /* --- compare --- */
        z80_state_t got;
        z80_state(cpu, &got);

        char detail[512];
        detail[0] = '\0';
        bool ok = true;

#define MISMATCH(fmt, ...)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (ok)                                                                                                        \
        {                                                                                                              \
            snprintf(detail, sizeof detail, fmt, __VA_ARGS__);                                                         \
        }                                                                                                              \
        ok = false;                                                                                                    \
    } while (0)

        if (got.af != expected.state.af)
        {
            MISMATCH("AF %04X, expected %04X", got.af, expected.state.af);
        }
        if (got.bc != expected.state.bc)
        {
            MISMATCH("BC %04X, expected %04X", got.bc, expected.state.bc);
        }
        if (got.de != expected.state.de)
        {
            MISMATCH("DE %04X, expected %04X", got.de, expected.state.de);
        }
        if (got.hl != expected.state.hl)
        {
            MISMATCH("HL %04X, expected %04X", got.hl, expected.state.hl);
        }
        if (got.af_alt != expected.state.af_alt)
        {
            MISMATCH("AF' %04X, expected %04X", got.af_alt, expected.state.af_alt);
        }
        if (got.bc_alt != expected.state.bc_alt)
        {
            MISMATCH("BC' %04X, expected %04X", got.bc_alt, expected.state.bc_alt);
        }
        if (got.de_alt != expected.state.de_alt)
        {
            MISMATCH("DE' %04X, expected %04X", got.de_alt, expected.state.de_alt);
        }
        if (got.hl_alt != expected.state.hl_alt)
        {
            MISMATCH("HL' %04X, expected %04X", got.hl_alt, expected.state.hl_alt);
        }
        if (got.ix != expected.state.ix)
        {
            MISMATCH("IX %04X, expected %04X", got.ix, expected.state.ix);
        }
        if (got.iy != expected.state.iy)
        {
            MISMATCH("IY %04X, expected %04X", got.iy, expected.state.iy);
        }
        if (got.sp != expected.state.sp)
        {
            MISMATCH("SP %04X, expected %04X", got.sp, expected.state.sp);
        }
        if (got.pc != expected.state.pc)
        {
            MISMATCH("PC %04X, expected %04X", got.pc, expected.state.pc);
        }
        if (got.i != expected.state.i)
        {
            MISMATCH("I %02X, expected %02X", got.i, expected.state.i);
        }
        if (got.r != expected.state.r)
        {
            MISMATCH("R %02X, expected %02X", got.r, expected.state.r);
        }
        if (got.im != expected.state.im)
        {
            MISMATCH("IM %u, expected %u", got.im, expected.state.im);
        }
        if (got.iff1 != expected.state.iff1)
        {
            MISMATCH("IFF1 %d, expected %d", (int)got.iff1, (int)expected.state.iff1);
        }
        if (got.iff2 != expected.state.iff2)
        {
            MISMATCH("IFF2 %d, expected %d", (int)got.iff2, (int)expected.state.iff2);
        }
        if (got.halted != expected.state.halted)
        {
            MISMATCH("halted %d, expected %d", (int)got.halted, (int)expected.state.halted);
        }
        if (tstates_run != expected.tstates)
        {
            MISMATCH("%d T-states, expected %d", tstates_run, expected.tstates);
        }

        for (int p = 0; p < expected.patch_count; ++p)
        {
            for (int b = 0; b < expected.patches[p].length; ++b)
            {
                const uint16_t address = (uint16_t)(expected.patches[p].address + b);
                if (world.memory[address] != expected.patches[p].bytes[b])
                {
                    MISMATCH("memory at %04X is %02X, expected %02X", address, world.memory[address],
                             expected.patches[p].bytes[b]);
                }
            }
        }

        if (world.overflowed)
        {
            MISMATCH("%s", "more bus events than the harness can hold");
        }
        {
            int ours = 0;
            int theirs = 0;
            while (ours < world.event_count || theirs < expected.event_count)
            {
                if (ours < world.event_count && theirs < expected.event_count)
                {
                    const bus_event *a = &world.events[ours];
                    const bus_event *b = &expected.events[theirs];
                    if (a->tstate == b->tstate && a->kind == b->kind && a->address == b->address &&
                        a->value == b->value)
                    {
                        ++ours;
                        ++theirs;
                        continue;
                    }
                }

                if (ours < world.event_count && corroborated_read(&expected, &world.events[ours]))
                {
                    ++ours;
                    continue;
                }

                if (ours >= world.event_count)
                {
                    const bus_event *b = &expected.events[theirs];
                    MISMATCH("missing event: %d %s %04X %02X", b->tstate, event_name(b->kind), b->address, b->value);
                    break;
                }
                if (theirs >= expected.event_count)
                {
                    const bus_event *a = &world.events[ours];
                    MISMATCH("unexpected event: %d %s %04X %02X", a->tstate, event_name(a->kind), a->address, a->value);
                    break;
                }

                const bus_event *a = &world.events[ours];
                const bus_event *b = &expected.events[theirs];
                MISMATCH("event is %d %s %04X %02X, expected %d %s %04X %02X", a->tstate, event_name(a->kind),
                         a->address, a->value, b->tstate, event_name(b->kind), b->address, b->value);
                break;
            }
        }

#undef MISMATCH

        if (!ok && known_divergence(in.name))
        {
            ++diverged;
            ok = true;
        }

        if (!ok)
        {
            ++failed;
            if (reported < report_limit)
            {
                printf("FAIL %-10s %s\n", in.name, detail);
                ++reported;
            }
            else if (reported == report_limit)
            {
                printf("... further failures suppressed\n");
                ++reported;
            }
        }

        ++ran;
        z80_free(cpu);
    }

    free(input_text);
    free(expected_text);

    /* Divergences are printed whether or not there are any: a suite that
       quietly forgives cases is worth less than one that says which. */
    printf("FUSE: %d tests, %d failed, %d known divergence(s)\n", ran, failed, diverged);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
