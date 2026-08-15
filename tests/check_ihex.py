#!/usr/bin/env python3
"""Assemble to Intel HEX and check the records decode back to the reference image.

The ihex writer reported failure on success for a long time, which nothing
caught because the only ihex coverage was "does it crash". This validates the
record structure, every checksum, and the decoded bytes.
"""

# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
#
# This file is part of Z80core, released under the terms of the GNU General
# Public License version 2. See LICENSE.md for the full text.
import argparse
import os
import subprocess
import sys
import tempfile

RECORD_DATA = 0x00
RECORD_EOF = 0x01


def parse_ihex(text):
    """Return the decoded image, or raise ValueError on a malformed file."""
    image = {}
    saw_eof = False
    for number, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        if not line:
            continue
        if saw_eof:
            raise ValueError(f"line {number}: record after the EOF record")
        if not line.startswith(":"):
            raise ValueError(f"line {number}: record does not start with ':'")
        try:
            raw = bytes.fromhex(line[1:])
        except ValueError:
            raise ValueError(f"line {number}: not hexadecimal: {line!r}")
        if len(raw) < 5:
            raise ValueError(f"line {number}: record too short")
        count, high, low, kind = raw[0], raw[1], raw[2], raw[3]
        if len(raw) != count + 5:
            raise ValueError(f"line {number}: byte count {count} does not match record length {len(raw) - 5}")
        if sum(raw) & 0xFF:
            raise ValueError(f"line {number}: checksum is wrong")
        if kind == RECORD_EOF:
            saw_eof = True
            continue
        if kind != RECORD_DATA:
            raise ValueError(f"line {number}: unexpected record type {kind:#04x}")
        address = (high << 8) | low
        for offset, value in enumerate(raw[4:-1]):
            image[address + offset] = value
    if not saw_eof:
        raise ValueError("file has no EOF record")
    return image


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zasm", required=True)
    ap.add_argument("--source", required=True)
    ap.add_argument("--reference", required=True)
    args = ap.parse_args()

    workdir = tempfile.mkdtemp(prefix="zasm_ihex_")
    output = os.path.join(workdir, "out.hex")

    result = subprocess.run([args.zasm, "-s", args.source, "-o", output, "-x", "ihex"],
                            capture_output=True, text=True, timeout=120)
    if result.returncode != 0:
        print(f"assembling to ihex failed ({result.returncode}):\n{result.stdout}{result.stderr}")
        return 1

    with open(output) as fh:
        text = fh.read()
    try:
        image = parse_ihex(text)
    except ValueError as error:
        print(f"malformed Intel HEX output: {error}")
        return 1

    with open(args.reference, "rb") as fh:
        expected = fh.read()

    if len(image) != len(expected):
        print(f"decoded {len(image)} bytes, reference image has {len(expected)}")
        return 1
    for address, value in sorted(image.items()):
        if address >= len(expected) or expected[address] != value:
            print(f"byte at {address:#06x} is {value:#04x}, reference has "
                  f"{expected[address]:#04x}" if address < len(expected) else "nothing")
            return 1

    print(f"Intel HEX output decodes to the reference image: {len(image)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
