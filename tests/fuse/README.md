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
- **`Q`** likewise — see issue #49.
- Interrupts, since there is no way to assert one in the input format.
