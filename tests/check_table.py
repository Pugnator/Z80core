#!/usr/bin/env python3
"""Check the invariants src/asmdasm/z80tab.c has to satisfy.

The opcode table is searched two different ways and both impose rules on it:

1. find_opcode() (assembler) does a binary search over the mnemonic, so the
   table must be sorted by mnemonic, and a mnemonic must have exactly one
   entry that is not flagged .duplicate - otherwise which encoding you get
   depends on where the search happens to land (this is how "IM 0" came to
   assemble as the undocumented ED4E).

   The disassembler instead files every row by (prefix page, opcode byte), so
   an opcode must be unambiguous and must sit on a page the index has an array
   for; one that does not is filed nowhere and silently stops decoding.

2. The disassembler formats the mnemonic with the operand bytes it read, so
   .data_size has to match the format string: no conversion means no operand,
   %#.2x means one byte, %#.4x means two.

3. The control-flow flags carry what reachability analysis runs on, and both
   ways of getting them wrong are silent. A row that should be flagged .stops
   and is not walks the analyser straight through a RET into whatever bytes
   follow; a missing .branches loses an entry point, and the code behind it is
   reported as data. So the set of stopping instructions is pinned exactly,
   in both directions, rather than merely spot-checked.

Run with the path to z80tab.c, or none to use the copy next to this script.
"""

# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
#
# This file is part of Z80core, released under the terms of the GNU General
# Public License version 2. See LICENSE.md for the full text.
import os
import re
import sys

ENTRY_RE = re.compile(r'\{\.opcode = (0x[0-9A-Fa-f]+), \.mnemo = "([^"]*)"([^}]*)\}')
CONV_RE = re.compile(r"%[#0-9.]*[xXd]")
# the prefix placeholders are not instructions and share one mnemonic
PLACEHOLDER = "****"

# The prefix pages the disassembler's decode index has an array for: no prefix,
# the four one-byte prefixes, and the two index-bit prefixes. An opcode outside
# these cannot be filed, so the table may not contain one.
INDEX_PAGES = {0x0000, 0x00CB, 0x00ED, 0x00DD, 0x00FD, 0xDDCB, 0xFDCB}

# Every instruction after which control does not reach the next one, and no
# others: the unconditional jumps and the returns. Conditional forms fall
# through when not taken, calls are expected back, and HALT resumes once an
# interrupt has been and gone, so none of those belong here.
STOPS = {
    0xC3,  # JP nn
    0x18,  # JR e
    0xC9,  # RET
    0xE9,  # JP (HL)
    0xDDE9,  # JP (IX)
    0xFDE9,  # JP (IY)
    0xED4D,  # RETI
    0xED45,  # RETN
    0xED55,  # and its undocumented encodings
    0xED5D,
    0xED65,
    0xED6D,
    0xED75,
    0xED7D,
}


def parse(path):
    src = open(path, encoding="utf8").read()
    entries = []
    for opcode, mnemo, rest in ENTRY_RE.findall(src):
        entries.append(
            {
                "opcode": int(opcode, 16),
                "mnemo": mnemo,
                "data_size": int(m.group(1)) if (m := re.search(r"\.data_size = (\d)", rest)) else 0,
                "duplicate": ".duplicate" in rest,
                "reljmp": ".reljmp" in rest,
                "branches": ".branches" in rest,
                "stops": ".stops" in rest,
            }
        )
    return entries


def expected_data_size(mnemo):
    convs = CONV_RE.findall(mnemo)
    if not convs:
        return 0
    if len(convs) == 2:
        # LD (IX+d),n: the table carries the displacement, the immediate is
        # handled as a special case in both directions
        return 1
    return 2 if "4" in convs[0] else 1


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "src", "asmdasm", "z80tab.c"
    )
    entries = parse(path)
    if not entries:
        print(f"no opcode entries found in {path}")
        return 1

    failures = []

    # 1a. sorted by mnemonic, case-insensitively, as find_opcode() assumes
    for prev, cur in zip(entries, entries[1:]):
        if cur["mnemo"].lower() < prev["mnemo"].lower():
            failures.append(f"table is not sorted: {prev['mnemo']!r} precedes {cur['mnemo']!r}")

    # 1b. exactly one primary encoding per mnemonic
    primaries = {}
    for entry in entries:
        if entry["mnemo"] == PLACEHOLDER or entry["duplicate"]:
            continue
        primaries.setdefault(entry["mnemo"].lower(), []).append(entry)
    for mnemo, group in sorted(primaries.items()):
        if len(group) > 1:
            opcodes = ", ".join(f"{e['opcode']:#x}" for e in group)
            failures.append(f"{mnemo!r} has {len(group)} entries without .duplicate: {opcodes}")

    # 1c. an opcode decodes to exactly one mnemonic, or which one you get
    #     depends on where it sits in the table
    by_opcode = {}
    for entry in entries:
        if entry["mnemo"] == PLACEHOLDER:
            continue
        by_opcode.setdefault(entry["opcode"], set()).add(entry["mnemo"])
    for opcode, names in sorted(by_opcode.items()):
        if len(names) > 1:
            failures.append(f"{opcode:#x} decodes to {len(names)} different mnemonics: {sorted(names)}")

    # 1d. every opcode belongs to a prefix page the decode index knows about.
    #     The index files a row by (page, byte); a row on a page it does not
    #     recognise is filed nowhere and the instruction silently stops
    #     decoding, where walking the table would still have found it. Keep in
    #     step with index_slot() in src/asmdasm/zdasm.c.
    for entry in entries:
        if entry["mnemo"] == PLACEHOLDER:
            continue
        page = entry["opcode"] >> 8
        if page not in INDEX_PAGES:
            failures.append(
                f"{entry['opcode']:#x} {entry['mnemo']!r}: prefix page {page:#x} is not one the decode index knows"
            )

    # 1e. the control-flow flags say what the instruction set says. These drive
    #     reachability analysis, and both mistakes are silent: a missing .stops
    #     walks the analyser through a RET into whatever follows, and a missing
    #     .branches loses an entry point and calls real code data.
    for entry in entries:
        if entry["mnemo"] == PLACEHOLDER:
            continue
        opcode, mnemo = entry["opcode"], entry["mnemo"]

        want_stops = opcode in STOPS
        if entry["stops"] != want_stops:
            verb = "must" if want_stops else "must not"
            failures.append(f"{opcode:#x} {mnemo!r}: {verb} be flagged .stops")

        # RST carries its target in the opcode, so it is the one branch with
        # no operand to take a target from
        if opcode <= 0xFF and (opcode & 0xC7) == 0xC7 and not entry["branches"]:
            failures.append(f"{opcode:#x} {mnemo!r}: RST must be flagged .branches")

        if entry["reljmp"] and not entry["branches"]:
            failures.append(f"{opcode:#x} {mnemo!r}: .reljmp without .branches")

        # a branch the decoder cannot work out a target for would silently get
        # the RST reading, bits 5..3 of its opcode times eight
        if entry["branches"] and not entry["reljmp"] and entry["data_size"] != 2:
            if not (opcode <= 0xFF and (opcode & 0xC7) == 0xC7):
                failures.append(
                    f"{opcode:#x} {mnemo!r}: .branches, but neither relative, "
                    f"nor a 2-byte address, nor a RST - no target to decode"
                )

    # 2. data_size agrees with the format string
    for entry in entries:
        want = expected_data_size(entry["mnemo"])
        if entry["data_size"] != want:
            failures.append(
                f"{entry['opcode']:#x} {entry['mnemo']!r}: data_size={entry['data_size']}, format needs {want}"
            )

    print(f"checked {len(entries)} opcode table entries")
    for failure in failures:
        print(f"FAIL: {failure}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
