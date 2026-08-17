#!/usr/bin/env python3
"""Disassemble a binary, reassemble the listing, and require the same bytes back.

This exercises both directions against each other: the disassembler has to
decode every instruction at the right length and print it in a form the
assembler accepts, and the assembler has to encode it back to the same bytes.
"""

# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
#
# This file is part of Z80core, released under the terms of the GNU General
# Public License version 2. See LICENSE.md for the full text.
import argparse
import os
import re
import subprocess
import sys
import tempfile


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zasm", required=True, help="path to the zasm executable")
    ap.add_argument("--binary", required=True, help="binary image to round-trip")
    ap.add_argument("--origin", default="0", help="origin the image is assembled at")
    ap.add_argument("--analyse", action="store_true", help="disassemble with code/data separation")
    args = ap.parse_args()

    workdir = tempfile.mkdtemp(prefix="zasm_roundtrip_")
    listing = os.path.join(workdir, "disasm.asm")
    rebuilt = os.path.join(workdir, "rebuilt.bin")

    command = [args.zasm, "-d"] + (["-a"] if args.analyse else []) + ["-s", args.binary]
    disasm = subprocess.run(command, capture_output=True, text=True, timeout=120)
    if disasm.returncode != 0:
        print(f"disassembly failed ({disasm.returncode}):\n{disasm.stdout}{disasm.stderr}")
        return 1

    # keep the instructions, drop the address comments the listing carries
    lines = [re.sub(r";.*$", "", line).rstrip() for line in disasm.stdout.splitlines()]
    source = "\n".join([f".org {args.origin}"] + [line for line in lines if line.strip()])
    with open(listing, "w") as fh:
        fh.write(source + "\n")

    asm = subprocess.run([args.zasm, "-s", listing, "-o", rebuilt], capture_output=True, text=True, timeout=120)
    if asm.returncode != 0:
        print(f"reassembly failed ({asm.returncode}):\n{asm.stdout}{asm.stderr}")
        print(f"listing kept at {listing}")
        return 1

    with open(args.binary, "rb") as fh:
        original = fh.read()
    with open(rebuilt, "rb") as fh:
        result = fh.read()

    if original != result:
        print(f"round trip differs: {len(original)} bytes in, {len(result)} bytes out")
        for offset, (a, b) in enumerate(zip(original, result)):
            if a != b:
                print(f"first difference at {offset:#06x}: {a:#04x} != {b:#04x}")
                break
        print(f"listing kept at {listing}")
        return 1

    print(f"round trip matches: {len(original)} bytes, {len(lines)} lines of disassembly")
    return 0


if __name__ == "__main__":
    sys.exit(main())
