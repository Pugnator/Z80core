# The FUSE Z80 test suite

`tests.in` and `tests.expected` are taken unmodified from **FUSE — the Free
Unix Spectrum Emulator**, by Philip Kendall and contributors,
<https://sourceforge.net/projects/fuse-emulator/>.

They are released under the **GNU General Public License version 2**, the same
licence as this project, and are redistributed here under those terms. See
[LICENSE.md](../../LICENSE.md) for the full text.

They are committed rather than downloaded because a conformance suite that CI
fetches from the network is a conformance suite that stops working the day the
network does — and because a test corpus that can change underneath you is not
a fixed point to measure against.

## What the suite is

1335 cases, one per instruction encoding including the prefixed and
undocumented ones. Each gives a complete starting machine, a memory image, and
the exact state and bus activity that must result.

It is the only widely used Z80 suite that records **bus events with the T-state
they occur on**, which is the property this core exists to have. ZEXALL passing
is often reported as "cycle accurate"; it says nothing whatever about timing.

## Format

`tests.in`, one block per test:

```
<name>
AF BC DE HL AF' BC' DE' HL' IX IY SP PC
I R IFF1 IFF2 IM halted tstates
<address> <byte>... -1        (repeated; a memory image to load)
-1
```

The `tstates` field is a *minimum*: run whole instructions until at least that
many T-states have passed. Almost every case asks for 1, meaning one
instruction.

`tests.expected`, one block per test:

```
<name>
<tstate> <event> <address> [<value>]     (repeated)
AF BC DE HL AF' BC' DE' HL' IX IY SP PC
I R IFF1 IFF2 IM halted tstates
<address> <byte>... -1        (repeated; memory that must have changed)
```

## Event types, and which ones we check

| | | Checked |
| --- | --- | --- |
| `MR` / `MW` | memory read / write, logged at the T-state its machine cycle **ends** | yes |
| `PR` / `PW` | port read / write, logged at the T-state `IORQ` goes active | yes |
| `MC` / `PC` | memory and port *contention* | **no** |

`MC` and `PC` are the Spectrum's ULA contending for the bus, not anything a
bare Z80 does. FUSE also uses them to mark internal T-states, and the address
it reports for those is its own bookkeeping rather than a claim about the pins
— `INC HL` logs `MC` against PC where this core holds the refresh address, and
both are defensible. Comparing them would be comparing our model to FUSE's
model of a different chip.

Nothing is lost by skipping them: the total T-state count catches an internal
cycle of the wrong length, and the `MR`/`MW` timestamps catch a bus cycle in
the wrong place.

## What the suite does not cover

- **`WZ`/MEMPTR** is not in the format at all. That needs z80test.
- **`Q`** likewise. Same consequence: see below.
- Interrupts, since there is no way to assert one in the input format.

## Where FUSE is wrong, and how we know

`BIT n,(HL)` takes its undocumented X and Y flags from the **high half of WZ**,
the same internal register the indexed form leaves holding `IX+d`. FUSE expects
them to follow the byte tested instead.

FUSE cannot be right here, and more importantly it cannot *know*: it has no
MEMPTR in its model or in its file format, so WZ is zero throughout its suite
and the two rules are indistinguishable in every case it contains. The
SingleStepTests corpus carries WZ in each case's initial state, varies it, and
pins the flags to the high half of it in **400 samples out of 400** — against
104 for the byte tested.

Eight of FUSE's cases reach a `BIT n,(HL)`; four of them use a WZ-independent
value that happens to agree, and four disagree. Those four are listed in
`known_divergence()` in the harness and reported on every run rather than
silently forgiven.

`SCF` and `CCF` are the same story with a different register. They take X and Y
from `A` OR'd with the flag bus residue — `Q ^ F` — and FUSE, having no Q,
chose `A` alone. SingleStepTests puts the full rule at 800 of 800 against 606
for `A`. Five more cases diverge, listed alongside the others.

This is the argument for having a second suite in one paragraph: the first one
was confidently and undetectably wrong, twice, in the same way — and each time
it was only something independent that could say so.
