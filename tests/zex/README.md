# The Zilog exerciser

`zexdoc.com` and `zexall.com` are Frank Cringle's Z80 instruction exerciser,
later maintained by Ian Collier and others, released under the **GNU General
Public License** — the same licence as this project, and redistributed here
under those terms. See [LICENSE.md](../../LICENSE.md).

They are CP/M `.COM` programs, 8704 bytes each, committed rather than fetched:
they are small, they never change, and a conformance suite that depends on the
network stops working the day the network does.

`zexdoc` checks the documented flag behaviour; `zexall` checks the undocumented
bits as well, and is the harder of the two.

## Running them

Off by default, because they take minutes rather than seconds:

```
cmake --preset default -DZ80CORE_SLOW_TESTS=ON
cmake --build --preset default
cd build/default && ctest -R zex
```

Or directly, which is what you want while fixing something:

```
build/default/bin/z80core_zex_test tests/zex/zexdoc.com
```

An optional second argument caps the run in millions of clock edges, which is
useful for checking it starts correctly without waiting for the whole thing.

## Why bother, given FUSE and SingleStepTests

Because those two test **one instruction at a time** from a synthesised machine
state. That is exactly what lets them check bus activity per T-state, and it is
also a blind spot: neither can catch a fault that only shows up when
instructions interact — a flag left behind that the next instruction reads,
state that leaks across an instruction boundary, a register restored a cycle
late.

This is the opposite shape. It is a real program running tens of millions of
instruction sequences and checking each block by CRC. It says **nothing
whatever about timing** — it is routinely and wrongly cited as proof of cycle
accuracy — but it exercises the thing the other two structurally cannot.

## The CP/M it needs, and where that lives

Two calls: print a character, and print a `$`-terminated string. Both are in
the harness, `src/emucore/z80core/tests/zex_test.c`, and none of it is in the
core — which still knows nothing about memory, files or a console.

The interception needs no hook in the core either. A `RET` is placed at the
BDOS entry and at the warm boot vector, and the harness watches for an **M1
cycle at those addresses**: an opcode fetch at a known address is the hook.

## Where it stands

`zexdoc` **passes**: all 67 blocks, `Tests complete`, no CRC mismatch, in 93.5
billion clock edges.

That number is the reason this is opt-in. An edge-stepped core dispatches twice
per T-state and the harness services the bus on every edge, so it runs about an
order of magnitude slower than an emulator working an instruction at a time.
That is the price of the timing this core exists to have, not something to tune
away. Budget tens of minutes.

## What counts as passing

The program prints its own verdict. The harness fails on either of two things:

- any `ERROR` in the output, which is a CRC mismatch;
- the absence of the closing `Tests complete` message, which means the run
  ended early and did not test what it claims to have tested.

The second matters more than it looks. Without it, a core that crashed halfway
through would print nothing alarming and pass.
