# References

What this project's behaviour is checked against, and what may be copied from.

## A-Z80

A gate-level reimplementation of the Z80 in Verilog by Goran Devic,
<https://github.com/gdevic/A-Z80>, released under the **GNU GPL version 2** —
the same licence as this project, so its material may be used here with
attribution.

Its timing table (`Timings.csv` in that project) lists the control signals
active in every T-state of every instruction class, along with register and ALU
activity, flag effects and `WZ` notes. It is what
[CPU-CORE-SPEC.md](CPU-CORE-SPEC.md) section 5.3 was checked against, and it
corrected two errors in the M1 cycle: `RFSH` rises with the end of the fetch
rather than half a cycle later, and the refresh asserts `MREQ`.

Reading it needs one convention: each row gives the signals active *at* that
T-state, so a transition happens on the edge before the row it first appears
in. Under that reading it agrees with the datasheet everywhere the two overlap.

A-Z80 is authoritative about **which T-state**. It is a conceptual
reimplementation rather than a transistor-level copy of the NMOS part, so the
half-cycle placement within a T-state still comes from the datasheet diagrams.

## Zilog Z80 CPU User Manual (UM0080)

The timing diagrams and AC characteristics. The diagrams settle the half-cycle
placement A-Z80 cannot; the AC table supplies the propagation delays that the
device script applies when publishing pins (spec section 4.3).

## Programming the Z80, Rodnay Zaks

Useful for instruction semantics and the programmer's model.

## Note on what lives in this repository

None of the above are committed here.

- The **Zaks book** is a copyrighted commercial publication. Keep a copy
  locally if you have one; it must not be redistributed through this
  repository.
- The **A-Z80 tree** is a complete FPGA project of its own. Referencing it by
  URL is better than vendoring several megabytes of Verilog we do not build.
- **`Timings.csv`** is small and directly useful, and its licence permits
  including it. It is left out for now because a copy taken into this tree
  needs its provenance and GPL 2 notice attached, not just the file. If we do
  want it in-tree, that is the work: add the notice, credit Goran Devic, and
  record it here.

`.gitignore` keeps all three out so they can sit in `docs/` locally without
being committed by accident.
