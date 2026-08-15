#!/usr/bin/env python3
"""Check the invariants src/asmdasm/z80tab.c has to satisfy.

The opcode table is searched two different ways and both impose rules on it:

1. find_opcode() (assembler) does a binary search over the mnemonic, so the
   table must be sorted by mnemonic, and a mnemonic must have exactly one
   entry that is not flagged .duplicate - otherwise which encoding you get
   depends on where the search happens to land (this is how "IM 0" came to
   assemble as the undocumented ED4E).

2. The disassembler formats the mnemonic with the operand bytes it read, so
   .data_size has to match the format string: no conversion means no operand,
   %#.2x means one byte, %#.4x means two.

Run with the path to z80tab.c, or none to use the copy next to this script.
"""
import os
import re
import sys

ENTRY_RE = re.compile(r'\{\.opcode = (0x[0-9A-Fa-f]+), \.mnemo = "([^"]*)"([^}]*)\}')
CONV_RE = re.compile(r"%[#0-9.]*[xXd]")
# the prefix placeholders are not instructions and share one mnemonic
PLACEHOLDER = "****"


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
