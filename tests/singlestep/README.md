# SingleStepTests

A **local** suite, not part of `ctest`. The corpus is about 280 MB and has to be
downloaded, so it is something you run when you want it rather than a gate that
turns a broken network into a broken build. [FUSE](../fuse/README.md) is the
per-commit gate and lives in the repository.

```
python tests/singlestep/run.py --fetch          # download and unpack, once
python tests/singlestep/run.py                  # run everything
python tests/singlestep/run.py --only 00 "cb 46"
python tests/singlestep/run.py --cases 50       # first 50 cases per opcode
```

It drives the core through `ctypes`, so the only thing to build is the shared
library the ordinary build already produces:

```
cmake --build --preset default --target z80core_shared
```

## Where it comes from

<https://github.com/SingleStepTests/z80> — JSMoo-derived, ~1000 files of 1000
cases each. `tests/singlestep/data/` is gitignored.

## Why it is worth having as well as FUSE

It records things FUSE has no concept of, and each of them is a gap in what we
could previously check:

| | |
| --- | --- |
| **`wz`** | MEMPTR, in every case's initial *and* final state. FUSE has none, so ours went unverified entirely. |
| **`q`** | The register behind the `SCF`/`CCF` flag quirk — issue #49. |
| **`iff1`, `iff2`, `im`, `ei`** | The interrupt state, including the pending-`EI` flag. |
| **per-T-state bus trace** | Address and data every T-state, not just at machine cycle boundaries. |

Getting it in immediately found a bug FUSE could not see: the **refresh address
was formed from `R` after its increment rather than before**, so every refresh
addressed one DRAM row too far. FUSE reports its own bookkeeping addresses for
those cycles, so it could never have caught it.

## What is compared, and what is not

Compared: the whole final register file including **WZ**, the interrupt state,
memory, and the **address bus on every T-state**.

Not compared:

- **The strobe columns.** The `rwmi` pin string sits on one canonical T-state
  per machine cycle and cannot express a strobe that begins on a falling edge —
  which is exactly what this core models. The address bus is driven for the
  whole cycle in both models, so it carries the check instead.
- **`q` and `p`**, until issue #49 implements Q.
- **`ei`**, until interrupts land (#51).

## Known divergences

- **`76` (`HALT`)** — this corpus does not model the halted state at all; it has
  no `halted` field and simply reports PC after the fetch. FUSE does model it,
  and expects PC to sit *on* the `HALT` so the CPU keeps re-fetching it, which
  is also what makes the bus correct. We follow FUSE here.

## Open findings, not yet resolved

- **`ed b0` (`LDIR`) and its family** disagree on the undocumented X and Y
  flags. Ours derive from bits 3 and 1 of `A + byte`; roughly three quarters of
  cases disagree, so the rule is wrong somewhere. Not yet investigated — see the
  method used for `BIT n,(HL)`: test each candidate source against the corpus
  and let the count decide rather than guessing.
