#!/usr/bin/env python3
"""Every instruction in the opcode table must be reachable from the grammar.

The table and the grammar are written separately, so an encoding can sit in
z80tab.c that no rule in z80.y can ever produce - the instruction looks
supported but cannot be assembled. This builds a source line out of each
entry's mnemonic and checks zasm emits the encoding the table promises.

Alias entries (.duplicate) are excluded on purpose: the assembler resolves a
mnemonic to its primary encoding, so an alias is only reachable through the
disassembler.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

ENTRY_RE = re.compile(r'\{\.opcode = (0x[0-9A-Fa-f]+), \.mnemo = "([^"]*)"([^}]*)\}')
CONV_RE = re.compile(r"%[#0-9.]*[xXd]")
PLACEHOLDER = "****"

DISPLACEMENT = 0x7F
IMMEDIATE = 0x42
ADDRESS = 0xAAAA
JUMP_TARGET = 0x40


def opcode_bytes(opcode):
    out = []
    while opcode:
        out.insert(0, opcode & 0xFF)
        opcode >>= 8
    return out or [0]


def source_and_expectation(opcode, mnemo, reljmp):
    """Turn a table entry into (source line, expected bytes)."""
    encoding = opcode_bytes(opcode)
    convs = CONV_RE.findall(mnemo)
    indexed_bit_op = len(encoding) == 3 and encoding[1] == 0xCB
    two_operand = opcode in (0xDD36, 0xFD36)

    if indexed_bit_op:
        # prefix CB displacement opcode
        return mnemo.replace(convs[0], hex(DISPLACEMENT), 1), bytes(
            [encoding[0], encoding[1], DISPLACEMENT, encoding[2]]
        )
    if two_operand:
        text = mnemo.replace(convs[0], hex(DISPLACEMENT), 1)
        text = text.replace(convs[1], hex(IMMEDIATE), 1)
        return text, bytes(encoding + [DISPLACEMENT, IMMEDIATE])
    if reljmp:
        # the operand is a target address; the instruction sits at origin 0
        text = mnemo.replace(convs[0], hex(JUMP_TARGET), 1)
        return text, bytes(encoding + [(JUMP_TARGET - len(encoding) - 1) & 0xFF])
    if not convs:
        return mnemo, bytes(encoding)
    if "4" in convs[0]:
        return mnemo.replace(convs[0], hex(ADDRESS), 1), bytes(encoding + [ADDRESS & 0xFF, ADDRESS >> 8])
    return mnemo.replace(convs[0], hex(DISPLACEMENT), 1), bytes(encoding + [DISPLACEMENT])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zasm", required=True)
    ap.add_argument("--table", required=True)
    args = ap.parse_args()

    workdir = tempfile.mkdtemp(prefix="zasm_coverage_")
    listing = os.path.join(workdir, "t.lst")
    binary = os.path.join(workdir, "t.bin")

    entries = []
    for opcode, mnemo, rest in ENTRY_RE.findall(open(args.table, encoding="utf8").read()):
        if mnemo == PLACEHOLDER or ".duplicate" in rest:
            continue
        entries.append((int(opcode, 16), mnemo, ".reljmp" in rest))

    unreachable, wrong = [], []
    for opcode, mnemo, reljmp in entries:
        line, expected = source_and_expectation(opcode, mnemo, reljmp)
        with open(listing, "w") as fh:
            fh.write(".org 0\n" + line + "\n.end\n")
        if os.path.exists(binary):
            os.remove(binary)
        result = subprocess.run([args.zasm, "-s", listing, "-o", binary], capture_output=True, text=True, timeout=30)
        got = b""
        if os.path.exists(binary):
            with open(binary, "rb") as fh:
                got = fh.read()
        if not got:
            message = (result.stdout + result.stderr).strip().splitlines()
            unreachable.append((opcode, line, message[0] if message else "no output"))
        elif got != expected:
            wrong.append((opcode, line, expected.hex(), got.hex()))

    for opcode, line, message in unreachable:
        print(f"FAIL {opcode:#08x} cannot be assembled: {line!r}: {message}")
    for opcode, line, expected, got in wrong:
        print(f"FAIL {opcode:#08x} {line!r}: expected {expected}, got {got}")

    ok = len(entries) - len(unreachable) - len(wrong)
    print(f"{ok}/{len(entries)} primary encodings assemble to the bytes the table specifies")
    return 1 if (unreachable or wrong) else 0


if __name__ == "__main__":
    sys.exit(main())
