#!/usr/bin/env python3
"""
Run the SingleStepTests Z80 suite against the core.

SPDX-License-Identifier: GPL-2.0-only
Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors

This is deliberately not part of ctest. The corpus is about 280 MB and has to
be downloaded, so it is a local suite you run when you want it, not a gate that
turns a broken network into a broken build. FUSE is the per-commit gate and
lives in the repository; see tests/fuse/README.md.

    python tests/singlestep/run.py --fetch          # download and unpack, once
    python tests/singlestep/run.py                  # run everything
    python tests/singlestep/run.py --only 00 cb46   # run named opcodes
    python tests/singlestep/run.py --cases 50       # first 50 cases per opcode

It drives the core through ctypes rather than a C harness, so there is nothing
to compile beyond the shared library the ordinary build already produces.
"""

import argparse
import ctypes
import io
import json
import os
import sys
import tarfile
import urllib.request

ARCHIVE_URL = "https://github.com/SingleStepTests/z80/archive/refs/heads/main.tar.gz"

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
DATA = os.path.join(HERE, "data")

# ---------------------------------------------------------------------------
# The core, loaded rather than linked
# ---------------------------------------------------------------------------


class Pins(ctypes.Structure):
    _fields_ = [("A", ctypes.c_uint16), ("D", ctypes.c_uint8), ("ctrl", ctypes.c_uint32)]


class State(ctypes.Structure):
    _fields_ = [
        ("af", ctypes.c_uint16), ("bc", ctypes.c_uint16),
        ("de", ctypes.c_uint16), ("hl", ctypes.c_uint16),
        ("af_alt", ctypes.c_uint16), ("bc_alt", ctypes.c_uint16),
        ("de_alt", ctypes.c_uint16), ("hl_alt", ctypes.c_uint16),
        ("ix", ctypes.c_uint16), ("iy", ctypes.c_uint16),
        ("sp", ctypes.c_uint16), ("pc", ctypes.c_uint16),
        ("wz", ctypes.c_uint16),
        ("i", ctypes.c_uint8), ("r", ctypes.c_uint8), ("im", ctypes.c_uint8),
        ("q", ctypes.c_uint8),
        ("iff1", ctypes.c_bool), ("iff2", ctypes.c_bool), ("halted", ctypes.c_bool),
        ("edges", ctypes.c_uint64),
    ]


Z80_MREQ = 1 << 1
Z80_IORQ = 1 << 2
Z80_RD = 1 << 3
Z80_WR = 1 << 4


def find_library():
    """The shared core, wherever the build put it."""
    names = ("z80core.dll", "libz80core.dll", "libz80core.so", "libz80core.dylib")
    roots = [os.path.join(ROOT, "build"), ROOT]
    for root in roots:
        for path, _dirs, files in os.walk(root):
            for name in names:
                if name in files:
                    return os.path.join(path, name)
    return None


def load_core():
    path = find_library()
    if not path:
        sys.exit(
            "no shared z80core found. Build it first:\n"
            "    cmake --build --preset default --target z80core_shared"
        )
    lib = ctypes.CDLL(path)
    lib.z80_new.restype = ctypes.c_void_p
    lib.z80_free.argtypes = [ctypes.c_void_p]
    lib.z80_reset.argtypes = [ctypes.c_void_p]
    lib.z80_tick.argtypes = [ctypes.c_void_p, ctypes.POINTER(Pins), ctypes.c_int]
    lib.z80_tick.restype = ctypes.c_uint32
    lib.z80_state.argtypes = [ctypes.c_void_p, ctypes.POINTER(State)]
    lib.z80_set_state.argtypes = [ctypes.c_void_p, ctypes.POINTER(State)]
    return lib, path


# ---------------------------------------------------------------------------
# The corpus
# ---------------------------------------------------------------------------


def fetch():
    """Download and unpack, once. Roughly 280 MB over the wire."""
    os.makedirs(DATA, exist_ok=True)
    print("fetching %s" % ARCHIVE_URL)
    print("this is about 280 MB and is kept in tests/singlestep/data, which is gitignored")

    with urllib.request.urlopen(ARCHIVE_URL) as response:
        payload = response.read()
    print("downloaded %.1f MB, unpacking" % (len(payload) / 1e6))

    written = 0
    with tarfile.open(fileobj=io.BytesIO(payload), mode="r:gz") as archive:
        for member in archive:
            if not member.isfile():
                continue
            parts = member.name.split("/")
            if len(parts) < 3 or parts[1] != "v1" or not parts[2].endswith(".json"):
                continue
            source = archive.extractfile(member)
            if source is None:
                continue
            with open(os.path.join(DATA, parts[2]), "wb") as target:
                target.write(source.read())
            written += 1
    print("unpacked %d opcode files into %s" % (written, DATA))


def corpus_files(only):
    if not os.path.isdir(DATA):
        sys.exit("no corpus. Run with --fetch first.")
    names = sorted(f for f in os.listdir(DATA) if f.endswith(".json"))
    if only:
        wanted = {name.lower() for name in only}
        names = [f for f in names if os.path.splitext(f)[0].lower() in wanted]
        if not names:
            sys.exit("none of %s found in the corpus" % ", ".join(only))
    return [os.path.join(DATA, f) for f in names]


# ---------------------------------------------------------------------------
# Running one case
# ---------------------------------------------------------------------------


def build_state(initial):
    state = State()
    state.af = (initial["a"] << 8) | initial["f"]
    state.bc = (initial["b"] << 8) | initial["c"]
    state.de = (initial["d"] << 8) | initial["e"]
    state.hl = (initial["h"] << 8) | initial["l"]
    state.af_alt = initial["af_"]
    state.bc_alt = initial["bc_"]
    state.de_alt = initial["de_"]
    state.hl_alt = initial["hl_"]
    state.ix = initial["ix"]
    state.iy = initial["iy"]
    state.sp = initial["sp"]
    state.pc = initial["pc"]
    state.wz = initial["wz"]
    state.i = initial["i"]
    state.r = initial["r"]
    state.im = initial["im"]
    state.q = initial["q"]
    state.iff1 = bool(initial["iff1"])
    state.iff2 = bool(initial["iff2"])
    state.halted = False
    return state


def port_reads(cycles):
    """
    The values an IN must be answered with, in order.

    Unlike FUSE, this corpus records what the port actually returned, so the
    harness has to replay it rather than invent a floating-bus value. The pin
    columns are "rwmi", and the byte lands on the entry after the strobe.
    """
    values = []
    for index, entry in enumerate(cycles):
        pins = entry[2] or ""
        if "r" in pins and "i" in pins:
            for later in cycles[index:]:
                if later[1] is not None:
                    values.append(later[1])
                    break
    return values


# This corpus has no notion of a halted CPU - no "halted" field - so for HALT
# it simply reports PC after the fetch. FUSE does model it and expects PC to
# sit on the HALT so the machine keeps re-fetching it, which is also what makes
# the bus right. We follow FUSE and skip PC here rather than pretend.
DIVERGENT = {"76": ("PC",)}


def run_case(lib, case, skip=()):
    """Returns a list of complaints, empty if the case passed."""
    memory = bytearray(0x10000)
    for address, value in case["initial"]["ram"]:
        memory[address] = value

    inputs = port_reads(case["cycles"])
    next_input = [0]

    cpu = lib.z80_new()
    try:
        lib.z80_reset(cpu)
        state = build_state(case["initial"])
        lib.z80_set_state(cpu, ctypes.byref(state))

        pins = Pins()
        level = 0
        addresses = []
        was_io_read = False

        # The reference says how long the instruction takes; run exactly that
        # and let the final state catch a core that disagrees - too long and it
        # is caught mid-instruction, too short and it has started the next one.
        for _ in range(len(case["cycles"])):
            for half in (0, 1):
                level ^= 1
                lib.z80_tick(cpu, ctypes.byref(pins), level)
                ctrl = pins.ctrl
                if ctrl & Z80_MREQ:
                    if ctrl & Z80_RD:
                        pins.D = memory[pins.A]
                    elif ctrl & Z80_WR:
                        memory[pins.A] = pins.D
                elif ctrl & Z80_IORQ:
                    if ctrl & Z80_RD:
                        index = next_input[0]
                        pins.D = inputs[index] if index < len(inputs) else 0
                if half == 0:
                    addresses.append(pins.A)
                if was_io_read and not (ctrl & Z80_IORQ):
                    next_input[0] += 1
                was_io_read = bool(ctrl & Z80_IORQ) and bool(ctrl & Z80_RD)

        final = State()
        lib.z80_state(cpu, ctypes.byref(final))
    finally:
        lib.z80_free(cpu)

    expected = case["final"]
    problems = []

    def check(name, got, want):
        if name in skip:
            return
        if got != want:
            problems.append("%s %04X, expected %04X" % (name, got, want))

    check("AF", final.af, (expected["a"] << 8) | expected["f"])
    check("BC", final.bc, (expected["b"] << 8) | expected["c"])
    check("DE", final.de, (expected["d"] << 8) | expected["e"])
    check("HL", final.hl, (expected["h"] << 8) | expected["l"])
    check("AF'", final.af_alt, expected["af_"])
    check("BC'", final.bc_alt, expected["bc_"])
    check("DE'", final.de_alt, expected["de_"])
    check("HL'", final.hl_alt, expected["hl_"])
    check("IX", final.ix, expected["ix"])
    check("IY", final.iy, expected["iy"])
    check("SP", final.sp, expected["sp"])
    check("PC", final.pc, expected["pc"])
    check("WZ", final.wz, expected["wz"])
    check("I", final.i, expected["i"])
    check("R", final.r, expected["r"])
    if final.im != expected["im"]:
        problems.append("IM %d, expected %d" % (final.im, expected["im"]))
    if final.iff1 != bool(expected["iff1"]):
        problems.append("IFF1 %d, expected %d" % (final.iff1, expected["iff1"]))
    if final.iff2 != bool(expected["iff2"]):
        problems.append("IFF2 %d, expected %d" % (final.iff2, expected["iff2"]))

    if final.q != expected["q"]:
        problems.append("Q %02X, expected %02X" % (final.q, expected["q"]))

    for address, value in expected["ram"]:
        if memory[address] != value:
            problems.append("memory at %04X is %02X, expected %02X" % (address, memory[address], value))

    # The address bus, T-state by T-state. Their strobe columns sit on one
    # canonical T-state per machine cycle and cannot express a strobe that
    # starts on a falling edge, so they are not compared - but the address is
    # driven for the whole cycle in both models, and it catches a machine cycle
    # in the wrong place or of the wrong length.
    for index, (entry, got) in enumerate(zip(case["cycles"], addresses)):
        want = entry[0]
        if want is not None and got != want:
            problems.append("T%d address %04X, expected %04X" % (index, got, want))
            break

    return problems


# ---------------------------------------------------------------------------


def main():
    parser = argparse.ArgumentParser(description="Run SingleStepTests against z80core")
    parser.add_argument("--fetch", action="store_true", help="download and unpack the corpus")
    parser.add_argument("--only", nargs="+", metavar="OPCODE", help="run only these opcode files")
    parser.add_argument("--cases", type=int, default=0, help="cases per opcode (0 = all)")
    parser.add_argument("--report", type=int, default=20, help="failures to print per opcode")
    args = parser.parse_args()

    if args.fetch:
        fetch()
        if not args.only and args.cases == 0:
            return 0

    lib, path = load_core()
    print("core: %s" % path)

    files = corpus_files(args.only)
    total = failed = 0
    worst = []

    for filename in files:
        with open(filename) as handle:
            cases = json.load(handle)
        if args.cases:
            cases = cases[: args.cases]

        name = os.path.splitext(os.path.basename(filename))[0]
        reported = 0
        opcode_failures = 0
        skip = DIVERGENT.get(name, ())
        if skip:
            print("note: %s skips %s, see tests/singlestep/README.md" % (name, ", ".join(skip)))

        for case in cases:
            problems = run_case(lib, case, skip)
            total += 1
            if problems:
                failed += 1
                opcode_failures += 1
                if reported < args.report:
                    print("FAIL %-8s %-12s %s" % (name, case["name"], "; ".join(problems[:3])))
                    reported += 1

        if opcode_failures:
            worst.append((opcode_failures, name, len(cases)))

    worst.sort(reverse=True)
    if worst:
        print("\nworst opcodes:")
        for count, name, size in worst[:20]:
            print("  %-8s %d/%d" % (name, count, size))

    print("\nSingleStepTests: %d cases, %d failed" % (total, failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
