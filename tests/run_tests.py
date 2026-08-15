#!/usr/bin/env python3
"""Opcode-suite runner: assembles each line of asm_test.asm and compares output bytes.

Python port of tests/test_asm.lua. Exit code 0 iff no failures.
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
    ap.add_argument("--zasm", required=True, help="path to zasm executable")
    ap.add_argument("--tests", required=True, help="path to asm_test.asm")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    workdir = tempfile.mkdtemp(prefix="zasm_test_")
    lst = os.path.join(workdir, "temp.lst")
    out = os.path.join(workdir, "out.bin")

    passed = failed = skipped = total = 0
    previous = None
    failures = []

    with open(args.tests) as f:
        lines = f.readlines()

    for line in lines:
        line = line.rstrip("\n")
        if not line or line.startswith(";"):
            continue
        m = re.match(r"^(.*?);\s*(0x[0-9a-fA-F]+)\s*#size:(\d)\s*$", line)
        if not m:
            continue
        total += 1
        op = m.group(1).strip()
        expected_hex = m.group(2)[2:].lower()
        expected_size = int(m.group(3))

        with open(lst, "w") as fh:
            fh.write(".org 0\nstart:\n" + line + "\n.end\n")

        try:
            r = subprocess.run([args.zasm, "-s", lst, "-o", out],
                               capture_output=True, text=True, timeout=10)
            stdout = r.stdout
        except subprocess.TimeoutExpired:
            stdout = "<timeout>"

        got = b""
        if os.path.exists(out):
            with open(out, "rb") as rf:
                got = rf.read()
            os.remove(out)

        got_hex = got.hex()
        ok = len(got) == expected_size and int(got_hex or "0", 16) == int(expected_hex, 16)

        if ok:
            passed += 1
        elif previous == op:
            skipped += 1  # alternative encodings of the instruction just tested
        else:
            failed += 1
            failures.append((op, expected_hex, got_hex, expected_size, len(got), stdout.strip()))
        previous = op

    print(f"Total {total}: {passed} PASSED, {failed} FAILED, {skipped} SKIPPED (alias encodings)")
    for op, exp, got, esz, gsz, msg in failures:
        print(f"FAIL: {op!r:45} expected {exp} ({esz}B) got {got or '<empty>'} ({gsz}B)"
              + (f" | {msg}" if msg else ""))
    return 1 if failed else 0

if __name__ == "__main__":
    sys.exit(main())
