# Z80 CPU core — specification

Status: **draft for review**. Nothing is implemented yet.

This describes `z80core`, a self-contained, externally clocked, edge-precise
Z80 CPU model written in **C**, shipped as a DLL, with Lua for tooling and for
the host that drives it. It is the CPU and nothing else: no memory, no devices,
no video, no sound, no scheduler. A host drives its clock one edge at a time,
reads its pins, and answers its bus requests. Put a memory map and a GPU kernel
beside it and you have a virtual machine; on its own it is a chip.

## 0. The stack

```
  Proteus 8                          simulation, schematic, the clock net
      |  VSM SDK (C++)
  openvsm.dll                        existing project: VSM model + Lua 5.4.6
      |  Lua model API (device_pins, pin:onchange, VDM)
  z80_device.lua                     wires pins to the core, owns VSM interaction
      |  require("z80core")
  z80core.dll                        THIS SPEC: the CPU, in C, plus a Lua binding
```

Each layer has one job. `openvsm` already solves VSM integration and is not
touched. The device script owns everything machine-specific: which pins, what
memory answers, how the board behaves. The core owns the Z80 and knows nothing
about any of it.

The same `z80core.dll` links directly into a C host with no Lua involved, which
is what makes it reusable beyond Proteus.

---

## 1. Scope

### 1.1 In scope

- The complete Z80 instruction set: documented and undocumented.
- Complete internal state, including the parts software can observe only
  indirectly (`WZ`/MEMPTR, `Q`, `IFF1`/`IFF2`, interrupt mode, refresh).
- Bus and control pins, driven and sampled with correct timing.
- Interrupt (`INT`), non-maskable interrupt (`NMI`), `RESET`, `WAIT`,
  `BUSRQ`/`BUSAK`, `HALT`, `RFSH`.
- Cycle-exact behaviour at clock-edge granularity, including the internal
  (no-bus-activity) cycles of every instruction.
- Snapshot and restore of the entire state.

### 1.2 Not in scope

The core never contains, allocates, or knows about any of these:

| Not in the core | Where it belongs |
| --- | --- |
| RAM, ROM, memory maps, banking | host |
| I/O devices, ULA, PSG, FDC | host |
| Video, audio, input | host |
| Contention tables, timing of a specific machine | host, via `WAIT` |
| Frame scheduling, run-for-N-cycles loops | host |
| Debugger UI, disassembly, symbol tables | separate tools (`zasm` already disassembles) |

The existing `include/emucore/cpu.hpp` sketch holds a `cached_ram` member.
That goes: memory is the host's, always.

### 1.3 First host: a Proteus VSM model

The core is host-agnostic. The **first** host — the one it will be brought up
and tested against — is a **Proteus VSM** model, a Windows DLL that Labcenter's
simulator loads and drives from the schematic.

First, not only. Nothing in the core is Proteus-specific, and the two decisions
Proteus did drive turn out to be good for any host:

- **The host supplies the clock level** (4.2). Useful to anything that can
  gate, halt or single-step a clock, which includes every debugger.
- **`tick()` reports which pins changed** (4.3). Useful to any event-driven
  host, and merely harmless to one that ignores it.

So Proteus sharpened the interface rather than narrowing it. What VSM gives us,
and what it demands:

| Proteus VSM | Consequence for this core |
| --- | --- |
| The simulator is event-driven; models are woken when pins change | The core must be a pure state machine with no internal clock, which is what D2 already requires |
| `CLK` is a real net driven by the schematic — it can be slowed, gated, or stopped, and the user can single-step it | The core must follow the **actual clock level** rather than assume alternating edges (section 4.2) |
| Outputs are *scheduled* at an absolute simulation time, with propagation delay | The device script needs to know **which pins changed on this edge**, not diff all of them (section 4.3) |
| Models are C++ DLLs against the VSM SDK | Already solved: `openvsm` is that DLL, and the core reaches Proteus through a Lua device script rather than a VSM model of its own (section 11.2) |
| The VDM bridge offers register and disassembly views in the debugger | The debug API of section 8 supplies the state and `zasm` supplies the disassembly (section 11.3) |
| Simulation is not real time and never claims to be | The performance target is *usable simulation speed*, not a hard deadline (section 10.1) |

Everything the model needs beyond the CPU — memory, ROM contents, peripherals —
is on the schematic or in the device script, where it belongs. The core stays a
chip.

### 1.4 What "done" means

The core is finished when it passes every suite in [section 9](#9-conformance)
with no exemptions, and sustains the throughput target in
[section 10](#10-performance).

---

## 2. Design decisions

These were settled before writing this spec; they are recorded with their
consequences so the reasoning survives.

### D1 — The core is C; Lua is for tooling and hosts

One implementation, in C, shipped as a DLL. Lua drives it, inspects it and
tests it, but never executes an instruction.

Three reasons, in the order they matter:

1. **Precision.** `uint8_t A` and `uint16_t PC` wrap correctly because the type
   says so. In Lua every result is a 64-bit integer that must be masked by hand
   on every operation, and the one place someone forgets is a silent wrong
   value that surfaces only if a test happens to cover it. Across 1270
   encodings, having the language enforce register width is worth more than any
   convenience it costs.
2. **No boundary in the hot path.** Ticking per clock edge means the step runs
   millions of times a second. A Lua core would cross the C boundary on every
   one of them; a C core crosses nothing.
3. **It inherits a working project.** `zasm` is C, with CMake, ctest, seven
   conformance tests and CI on two platforms. The core reuses all of it —
   including the disassembler, for the debug view.

What Lua keeps, because it is genuinely better there: the device script that
owns VSM interaction, peripherals and boards, scripted inspection of a running
core, and test harnesses.

Consequence: maintainability has to come from **the instruction table being
data** (section 5.4) rather than from a high-level language. A wall of
hand-written C would forfeit exactly what this decision is supposed to protect.

### D2 — Pin-level, one call per clock **edge**

`tick()` advances the CPU by exactly one clock edge: half a T-state. The host
observes and drives pins between ticks. This mirrors the real part, makes
`WAIT` states and machine-specific contention a host concern rather than a core
concern, and is the interface a VM wants when a GPU kernel shares the same
clock.

Edges rather than whole T-states because that is where the chip actually acts.
`/MREQ` and `/RD` fall half a cycle after the address is valid; `/WAIT` is
sampled on a falling edge; an I/O cycle differs from a memory cycle precisely
by asserting `/IORQ` half a cycle later. A T-state-granular model has to
approximate all of that, and anything sharing the bus at sub-cycle resolution
would see the approximation.

Consequence: this is the most demanding interface possible — two cross-boundary
interactions per clock cycle, so 7 million per second at 3.5 MHz. Section 10
makes the cost budget explicit, and Phase 1 measures it before the instruction
set is written.

### D3 — Everything, undocumented included

Undocumented opcodes, `WZ`/MEMPTR, `Q`, `XF`/`YF`, the block-instruction flag
oddities, exact interrupt acceptance timing. `zasm` already assembles the
undocumented set; a core that cannot run what the assembler emits would be a
strange pairing.

### D4 — Lua 5.4, and only 5.4, wherever Lua appears

The device script, the binding and the tooling target **Lua 5.4** — the version
`openvsm` embeds (5.4.6). No LuaJIT anywhere.

This closes a question that was open while the core was going to be Lua.
LuaJIT is Lua 5.1 semantics with no native bitwise operators, so `x & 0xFF` is
a syntax error there while being idiomatic in 5.4; supporting both would have
meant routing every mask and shift through a shim. Targeting 5.4 alone means
native bitwise operators and real integers, which is the pleasant end of that
trade — and with the core in C, none of it is on a hot path anyway.

A microcontroller build remains explicitly out of scope. It is now merely a
port of a C library rather than an impossibility, but it is a separate project
with its own decisions, not a stretch goal of this one.

---

## 3. Pins

The model exposes the Z80's real pins. Names follow the datasheet. Active-low
signals are named without the bar and are **true when asserted**, which is the
opposite of the electrical level; this is stated once here and applies
everywhere.

| Pin | Dir | Width | Meaning |
| --- | --- | --- | --- |
| `A` | out | 16 | Address bus |
| `D` | in/out | 8 | Data bus |
| `M1` | out | 1 | Opcode fetch cycle |
| `MREQ` | out | 1 | Memory request |
| `IORQ` | out | 1 | I/O request; with `M1`, interrupt acknowledge |
| `RD` | out | 1 | Read strobe |
| `WR` | out | 1 | Write strobe |
| `RFSH` | out | 1 | Refresh address on `A0..A6` |
| `HALT` | out | 1 | CPU is halted |
| `BUSAK` | out | 1 | Bus acknowledged, buses floating |
| `WAIT` | in | 1 | Host asks the CPU to stretch the current cycle |
| `INT` | in | 1 | Maskable interrupt request |
| `NMI` | in | 1 | Non-maskable interrupt request (edge triggered) |
| `RESET` | in | 1 | Reset request |
| `BUSRQ` | in | 1 | Bus request |

Notes:

- `D` is bidirectional. The core drives it during a write; the host drives it
  during a read and during interrupt acknowledge. Section 5.3 defines exactly
  when the core samples it.
- `NMI` is edge triggered: the core latches a falling edge and clears the latch
  when it services the request. `INT` is level sensitive and is sampled only at
  the points in section 6.
- `BUSAK` asserted means the core has tri-stated `A`, `D`, `MREQ`, `IORQ`, `RD`,
  `WR` and `RFSH`. The model represents "floating" explicitly rather than
  leaving stale values, so a host cannot accidentally read a bus the CPU is not
  driving.

---

## 4. Interface

The C API is the interface. The Lua binding is a thin wrapper over it, so
anything true of one is true of the other.

### 4.1 The C API

```c
#include "z80core.h"

typedef struct {
    uint16_t A;         /* address bus            */
    uint8_t  D;         /* data bus, bidirectional */
    uint32_t ctrl;      /* control pins, Z80_M1 | Z80_MREQ | ... */
} z80_pins_t;

z80_t   *z80_new(void);
void     z80_free(z80_t *cpu);
void     z80_reset(z80_t *cpu);

/* advance to the next clock edge; clk is the new level, 0 or 1.
   returns the mask of output pins driven to a new value (4.3) */
uint32_t z80_tick(z80_t *cpu, z80_pins_t *pins, int clk);
```

The constructor takes no callbacks, no memory and no configuration. A CPU is a
CPU. The host owns `z80_pins_t`: it writes the inputs, calls `z80_tick`, then
reads the outputs.

### 4.2 The tick

`z80_tick()` reads the input pins, advances the machine to the next clock edge,
and updates the output pins. It never calls back into the host and never
allocates. Everything the host needs is in the pins afterwards; everything the
core needs the host puts there beforehand.

Two ticks make one T-state, and the host passes the **new clock level** rather
than the core owning the phase. Calling with the level the core is already at
is a no-op, not an error: a clock that is stopped, gated or stepped by hand
simply does not advance the CPU, which is what the hardware does.

That matters because `CLK` is a real net on a Proteus schematic. It can be
halted, single-stepped, run at 1 Hz for debugging, or driven by something other
than a clean oscillator, and a core that assumed alternating edges would
silently desynchronise from the net it is wired to. One comparison per tick is
a small price for following the actual clock.

A host answers the bus between ticks:

```c
uint32_t changed = z80_tick(cpu, &pins, clk);

if (pins.ctrl & Z80_MREQ) {
    if (pins.ctrl & Z80_RD)  pins.D = memory[pins.A];   /* valid before the sampling edge */
    if (pins.ctrl & Z80_WR)  memory[pins.A] = pins.D;   /* latch while WR is asserted */
}
```

A host that models nothing sub-cycle can read and write on any edge where the
strobe is asserted, because the core holds its bus signals for the whole of
their defined window. A host that does care — a ULA, a GPU kernel sharing the
bus — has every edge available to it.

### 4.3 What changed on this edge

`z80_tick()` returns a bitmask of the output pins it drove to a new value, and
`0` when it drove none.

This exists because of how a VSM model publishes outputs. Proteus does not want
"here is the state of every pin"; it wants "this pin takes this value at this
time", scheduled with the propagation delay for that signal — `MREQ` falling
has a different delay from the address bus becoming valid. Without the mask the
device script would have to diff thirty pins on every edge to recover
information the core already had.

Most edges change nothing, so the common case is a compare against zero:

```lua
local changed = cpu:tick(clk)
if changed ~= 0 then
  publish(changed)          -- schedule only what moved, each with its own delay
end
```

The core reports *what* changed. It never expresses *when* in analog time:
propagation delays belong to the device script, being a property of the part
being modelled — and of its temperature and supply voltage — not of the
instruction set.

### 4.4 The Lua binding

`z80core.dll` is also a Lua 5.4 C module, so a device script loads it directly:

```lua
local z80 = require("z80core")

local cpu = z80.new()
local changed = cpu:tick(clk)     -- returns the mask of 4.3
cpu.pins.D = memory[cpu.pins.A]   -- pins as a userdata with named fields
```

The binding exposes exactly the C API plus the debug surface of section 8. It
does not reimplement anything, and it is not on any critical correctness path:
if the binding and the C API ever disagree, the C API is right.

### 4.5 Events

"Event-driven" here means the core is a state machine that moves only when
clocked, and everything it communicates is a pin change. On top of that it can
emit *notifications* that a host would otherwise reconstruct by watching pins:
instruction fetch, interrupt accepted, halt, reset.

Rules that keep this honest:

1. Notifications are **derived**, never authoritative. A host that ignores them
   entirely sees identical behaviour.
2. With no listener registered the hot path must not test for one per tick;
   dispatch is switched when a listener is registered (section 10.3).
3. Handlers must not mutate CPU state. Section 8 is the supported way to do
   that.

---

## 5. Execution model

### 5.1 State machine, not an interpreter loop

The core holds a **step cursor**. Each instruction is a list of steps, one per
clock edge. `tick()` executes the step at the cursor and advances it. There is
no "execute instruction" function, because an instruction never executes at a
single moment: it is spread across its edges, and the bus activity is what the
outside world sees.

```
step list for LD A,(HL)   -- 7 T-states = 14 edges: M1 of 4, memory read of 3

     edge      action
  1  T1 rise   A <- PC; M1 asserted
  2  T1 fall   MREQ, RD asserted
  3  T2 rise   -
  4  T2 fall   sample WAIT; if asserted, hold here
  5  T3 rise   latch opcode from D; MREQ, RD, M1 released
  6  T3 fall   A <- I:R; RFSH asserted; R incremented
  7  T4 rise   decode
  8  T4 fall   RFSH released
  9  T1 rise   A <- HL
 10  T1 fall   MREQ, RD asserted
 11  T2 rise   -
 12  T2 fall   sample WAIT; if asserted, hold here
 13  T3 rise   A_reg <- D
 14  T3 fall   MREQ, RD released
```

Step lists are built once, at construction, from the instruction table
(section 5.4) — a static array of step functions per encoding. At run time the
core indexes that array and calls through it; it never parses, allocates, or
branches on a description.

This is what keeps a C core maintainable (D1). The table is the readable
artifact and the engine is small and generic; nobody hand-writes 1270 switch
cases, and adding an instruction means adding a row.

Edges where nothing happens still cost a step. That is deliberate: a uniform
one-step-per-edge cursor keeps the hot path branch-free and monomorphic, which
matters more than skipping a handful of no-op calls (section 10.3). An
optimisation that collapses idle edges is possible later, but only with the
conformance suite already green, so it can be proven not to change behaviour.

### 5.2 WAIT

`WAIT` is sampled on the **falling edge of T2** in M1 and memory cycles, and on
the falling edge of the automatically inserted wait state in I/O cycles. If it
is asserted, the cursor does not advance: the core repeats a wait T-state,
holding every pin as it stands, and samples again on the next falling edge.

This is the whole contention mechanism. The core knows nothing about the ZX
Spectrum's ULA or any other machine's timing; a host that wants contention
asserts `WAIT`, and the timing that results is correct by construction.

### 5.3 Bus cycle timing

Each cycle is defined as a sequence of edge actions. `↑` is a rising edge, `↓`
falling.

**M1, opcode fetch — 4 T-states**

| Edge | Core | Host |
| --- | --- | --- |
| T1 ↑ | `A` ← PC, `M1` asserted | |
| T1 ↓ | `MREQ`, `RD` asserted | |
| T2 ↓ | samples `WAIT` | assert `WAIT` here to stretch |
| T3 ↑ | samples `D`; releases `M1`/`MREQ`/`RD`; `A` ← `I:R`, `RFSH` asserted, `R` incremented | opcode must be valid on `D` |
| T3 ↓ | `MREQ` asserted again, for the refresh cycle | |
| T4 ↓ | `MREQ` and `RFSH` released | |

The refresh is a real memory cycle, not merely an address on the bus: `MREQ`
goes active for it in T4. Anything decoding memory accesses from `MREQ` alone
will see it, which is exactly what a DRAM is supposed to do and what a careless
address decoder will trip over.

**Memory read — 3 T-states**

| Edge | Core | Host |
| --- | --- | --- |
| T1 ↑ | `A` ← address | |
| T1 ↓ | `MREQ`, `RD` asserted | |
| T2 ↓ | samples `WAIT` | assert `WAIT` here to stretch |
| T3 ↑ | samples `D` | data must be valid on `D` |
| T3 ↓ | `MREQ`, `RD` released | |

**Memory write — 3 T-states**

| Edge | Core | Host |
| --- | --- | --- |
| T1 ↑ | `A` ← address | |
| T1 ↓ | `MREQ` asserted | |
| T2 ↑ | `D` ← data | |
| T2 ↓ | `WR` asserted; samples `WAIT` | latch `D` any time `WR` is asserted |
| T3 ↓ | `WR`, `MREQ` released | |

**I/O read and write — 4 T-states**

An I/O cycle differs from a memory cycle in two ways, and both are visible only
at edge resolution: `IORQ` is asserted half a cycle later — on the **rising**
edge of T2, not the falling edge of T1 — and the CPU inserts one wait state
automatically, which is why `IN`/`OUT` cost 11 or 12 T-states rather than 10.

| Edge | Core | Host |
| --- | --- | --- |
| T1 ↑ | `A` ← port address (and `D` ← data, for a write) | |
| T2 ↑ | `IORQ` + (`RD` or `WR`) asserted | |
| Tw ↓ | samples `WAIT` | assert `WAIT` here to stretch |
| T3 ↑ | samples `D` (read) | data must be valid on `D` |
| T3 ↓ | `IORQ`, `RD`/`WR` released | |

**Interrupt acknowledge — 6 T-states.** An M1 cycle with `M1` and `IORQ`
asserted together, extended by two T-states so a daisy chain has time to
settle. The host places the vector or instruction on `D`.

**Internal cycles.** One or more T-states with no bus signal asserted. They are
real: they are why `INC BC` costs 6 T-states rather than 4, and they must appear
in the step list at the right position, not be added as a lump at the end.

**Sources.** The Zilog *Z80 CPU User Manual* timing diagrams are the reference,
and the conformance suites in section 9 are the arbiter for anything they leave
ambiguous. The tables above were checked against the **A-Z80** timing table
(`docs/Timings.csv`), a gate-level Z80 reimplementation in Verilog by Goran
Devic, which records the active signals for every T-state of every instruction
class. A-Z80 is GPL 2, the same licence as this project.

Reading it requires one interpretation: its rows give the signals active at
each T-state, which places every transition on the edge *before* the row it
first appears in. Under that reading it confirmed the memory read, memory
write and I/O cycles above exactly as written, and corrected the M1 cycle in
two places — `RFSH` rises with the end of the fetch rather than half a cycle
later, and the refresh asserts `MREQ`, which this spec previously missed
altogether.

That interpretation is the remaining soft spot: A-Z80 is authoritative about
*which T-state*, while the half-cycle placement within it comes from the
datasheet diagrams. Where the two could disagree, the datasheet wins and the
suites decide.

### 5.4 The instruction table

One table describes every encoding: its operands, the M-cycles it performs in
order, the internal cycles between them, its effect on registers and flags, and
its `WZ` behaviour. This is the only place instruction knowledge lives; the
engine is generic.

`zasm`'s `src/asmdasm/z80tab.c` already enumerates all 1270 encodings with
their mnemonics, and `tests/check_table.py` keeps it honest. The CPU table is a
separate artifact — it describes *behaviour and timing*, not text — but the two
must agree on which encodings exist, and a test will assert exactly that.

### 5.5 Prefixes

`DD`, `FD`, `CB`, `ED` and the `DDCB`/`FDCB` pairs are handled as the hardware
does, not as a lookup shortcut:

- Each `DD`/`FD` is its own M1 cycle: 4 T-states, `R` incremented, and it can be
  chained (`DD DD FD 7E` is legal and each prefix costs its cycle).
- A `DD`/`FD` prefix followed by an opcode with no indexed form is a no-op
  prefix, and the following opcode executes normally.
- In `DDCB`/`FDCB`, the displacement is fetched **before** the opcode byte, and
  that final opcode fetch is *not* an M1 cycle: it does not increment `R`.
- An interrupt is never accepted between a prefix and its opcode.

### 5.6 Refresh

`R` increments on every M1 cycle, including the M1 of an interrupt
acknowledge, and bit 7 is never changed by the increment. During T3/T4 of M1
the core places `I:R` on the address bus and asserts `RFSH`. Hosts that do not
model DRAM can ignore `RFSH` entirely; software that reads `R` for timing or
randomness must still see the right values.

---

## 6. Interrupts, reset, halt

| Event | Sampled | Latency | Effect |
| --- | --- | --- | --- |
| `INT` | last T-state of an instruction, if `IFF1` set | mode dependent | acknowledge cycle, then mode 0/1/2 sequence |
| `NMI` | falling edge latched any time; acted on at instruction boundary | 11 T | `IFF1` saved to `IFF2`, `IFF1` cleared, `CALL 0x0066` |
| `RESET` | continuously | 3 T asserted minimum | state per 7.1 |
| `BUSRQ` | end of the current machine cycle | 1 cycle | `BUSAK`, buses float, CPU frozen |

Required behaviours, each of which is a common source of emulator bugs and each
of which will have a test:

- `EI` does not allow an interrupt before the **following** instruction
  completes, so `EI; RET` cannot be interrupted between the two.
- `INT` is not accepted between a prefix and its opcode (5.5).
- Interrupt mode timings: IM 0 executes the instruction placed on the bus
  (typically `RST`, 13 T for the usual case), IM 1 is 13 T, IM 2 is 19 T,
  and NMI is 11 T.
- `HALT` executes NOPs with `PC` held; on wake, execution resumes at the
  instruction after the `HALT`.
- A pending `NMI` wakes `HALT` even when `IFF1` is clear.

---

## 7. State

### 7.1 Register file

`AF BC DE HL`, the alternate set `AF' BC' DE' HL'`, `IX IY SP PC`, `I R`,
`IFF1 IFF2 IM`, and the internal `WZ` (MEMPTR) and `Q`.

Power-on and reset state: `PC = 0`, `SP = 0xFFFF`, `AF = 0xFFFF`, `I = 0`,
`R = 0`, `IFF1 = IFF2 = 0`, `IM = 0`. Reset takes effect at the end of the
current machine cycle and the first fetch after it is from address 0.

The existing sketch resets `PC` to `0xFFFF`; that is wrong and the spec is
authoritative here.

### 7.2 WZ (MEMPTR) and Q

Both are invisible to `LD` but observable through flags:

- `WZ` is set by a long list of instructions in instruction-specific ways, and
  is read back by `BIT n,(HL)` through `XF`/`YF`. The rules are those in
  *MEMPTR, esoteric register of the Zilog Z80 CPU*; conformance is defined by
  the test suite, not by prose here.
- `Q` holds whether the last instruction modified the flags, and drives the
  `SCF`/`CCF` `XF`/`YF` behaviour.

### 7.3 Snapshot

```c
size_t z80_save(const z80_t *cpu, void *buffer, size_t size);  /* versioned blob */
bool   z80_load(z80_t *cpu, const void *buffer, size_t size);
```

```lua
local snap = cpu:save()     -- string, serialisable, version tagged
cpu:load(snap)
```

A snapshot captures the step cursor and the clock phase, so a machine can be
saved between any two **edges**, not merely between instructions. Round-tripping
must be bit-exact, and a test asserts that saving, restoring and continuing
produces an identical trace.

---

## 8. Debug API

Separate from the hot path, and permitted to be slow.

```c
uint16_t z80_get(const z80_t *cpu, z80_reg_t which);
void     z80_set(z80_t *cpu, z80_reg_t which, uint16_t value);
void     z80_state(const z80_t *cpu, z80_state_t *out);   /* whole register file */
```

```lua
cpu:get("HL")        cpu:set("HL", 0x8000)
cpu:registers()      -- the register file as a table
cpu:position()       -- current opcode, prefix, M-cycle, edge within it
```

Read and write access to everything, including `WZ` and `Q`, because a debugger
that cannot see the whole machine is a debugger that lies. This is what feeds
`openvsm`'s VDM bridge, and with `zasm` supplying disassembly it is enough for
a register and source view inside Proteus without either project growing a
second disassembler.

---

## 9. Conformance

Correctness is defined by suites, not by opinion. All of these must pass, and
each is wired into `ctest` beside the existing assembler tests.

| Suite | What it proves | Why this one |
| --- | --- | --- |
| **FUSE** `tests.in`/`tests.expected` | Per-instruction final state **and the full list of bus events with their T-state**. | The only widely used suite that checks pin-level timing, which is exactly what D2 buys us. The primary gate. |
| **ZEXDOC / ZEXALL** | Documented, then undocumented, instruction and flag behaviour, by CRC over millions of cases. | The classic bar. Needs a tiny CP/M stub in the *test harness*, never in the core. |
| **z80test** (Patrik Rak) | Flags and `MEMPTR` in more depth than ZEXALL, including `SCF`/`CCF` and block instructions. | Catches `WZ`/`Q` errors ZEXALL misses. |
| **TomHarte / SingleStepTests** | 10k randomised cases per opcode, each with cycle-by-cycle bus activity. | Exhaustive breadth; good as a nightly job rather than a per-commit one, given its size. |
| **Internal** | Snapshot round-trip, `WAIT` stretching, `BUSRQ`, reset timing, EI/prefix interrupt rules, C API and Lua binding agreement. | The behaviours the public suites do not reach. |

A note on honesty: ZEXALL passing is frequently reported as "cycle accurate".
It is not — it says nothing about timing. FUSE and SingleStepTests are what
justify that claim, which is why they are the gate.

---

## 10. Performance

### 10.1 Target

The nominal figure is **7 million `tick()` calls per second** — real time for a
3.5 MHz Z80. Treat it as a design guide, not a deadline, for two separate
reasons.

First, Proteus VSM simulation is not real time and does not pretend to be. A
schematic with analog parts runs far slower than the hardware it models, and
nobody is surprised. Missing the number makes the model sluggish to
single-step; it does not make it wrong.

Second, and more useful: **the simulator, not the core, will set the ceiling.**
A 3.5 MHz clock net is 7 million digital events per second for Proteus to
schedule before our model does any work at all, and every address, data and
control transition we publish is another event in its queue. Realistically a
Z80 schematic in Proteus runs orders of magnitude below real time, and that is
normal for the tool.

So the honest target is not an absolute rate. It is: **the core must not be the
bottleneck.** Our cost per edge should be small next to what Proteus already
spends per event. Phase 0 measures the simulator's ceiling before we tune
anything against a number we invented.

### 10.2 Budget

On a 3.5 GHz machine, 7 M ticks/s leaves **about 500 CPU cycles per tick** for
everything: the core's step, the host's bus handling, and every device on the
clock. For a C step that reads a table entry and writes a few struct fields
that is an enormous budget — the core should use a small fraction of it.

Choosing C (D1) moved the performance risk off the core almost entirely. What
remains is the boundary: at 7 M edges per second, the Lua device script is
called by `openvsm` and calls into `z80core` on every edge, and those crossings
cost far more than the step itself. Two consequences:

- The device script's per-edge work is the thing to keep lean, not the core's.
- The mask of section 4.3 exists precisely so the script can decide in one
  compare whether an edge needs any further work at all.

The edge decision (D2) doubled the number of crossings. If it proves too
expensive in practice, the fallbacks are known and unchanged: collapse idle
edges, or fall back to T-state ticks with an edge-resolved path only for the
cycles that need it. Phase 0 measures this before anything depends on it.

### 10.3 Rules for the hot path

1. No allocation per tick, in either language. Step tables and state are built
   once, at construction.
2. No callbacks from the core into the host during a tick. The pins are the
   entire interface.
3. Event dispatch is chosen when a listener is registered, not tested per tick.
4. A single, uniform step signature so the compiler can keep the dispatch
   cheap.
5. In the device script: no table allocation, no string formatting, no
   `pcall` per edge. Everything the script needs is preallocated in
   `device_init()`.

### 10.4 Measurement

Two benchmarks, and both exist before the instruction set does:

1. **Through the whole stack** (Phase 0): Proteus to `openvsm` to the device
   script to `z80core`, with a stub core. This gives the simulator's ceiling
   and the real cost of a round trip across both boundaries. Everything else is
   tuning against a guess until this number exists.
2. **The core alone** (Phase 1): ticks/second for a skeleton CPU with no host,
   reported in CI, on x86 and x86-64. This isolates the core's own cost from
   the boundary's.

The interesting figure is the ratio between them. If the stack costs 100x what
the core costs, optimising the core is wasted effort and the device script is
where the work belongs.

---

## 11. Packaging

### 11.1 z80core.dll

One artifact, two ways to use it:

- **A Lua 5.4 C module.** It exports `luaopen_z80core`, so a device script
  loads it with `require("z80core")` (4.4). This is how Proteus reaches it.
- **A plain C library.** Link it, include `z80core.h`, call `z80_tick` (4.1).
  No Lua involved, and no Lua required to build this configuration.

Built for **x86 and x86-64**. 32-bit exists because Proteus is a 32-bit
application, not because the core needs it; a C host on either width is equally
supported. The pin layout uses fixed-width fields only — never a pointer or a
`long` — so the two builds cannot disagree about it, and a test asserts they do
not.

### 11.2 How it reaches Proteus

```
Proteus 8  ->  openvsm.dll  ->  z80_device.lua  ->  z80core.dll
```

`openvsm` is an existing, working project and is **not modified**. The device
script declares the pins, registers a callback on `CLK`, calls `cpu:tick(level)`
on each edge, publishes whatever the returned mask says changed, and answers
memory and I/O from the schematic. Everything machine-specific lives there;
nothing machine-specific reaches the core.

**The linkage question, which this arrangement depends on.** `openvsm` links
Lua 5.4.6 statically into its own DLL and opens the standard library, so
`require` exists. For `z80core.dll` to load into that interpreter it must
resolve the `lua_*` symbols, and the only supply is `openvsm.dll` itself, which
does export them (`CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS` is on). That should work,
and it is unusual enough to be worth proving before anything is built on it.
Phase 0 does exactly that, and the fallbacks in descending order of preference
are:

1. Link `z80core.dll` against the Lua symbols `openvsm.dll` exports.
2. Build Lua as a shared `lua54.dll` that both link against — a small change to
   `openvsm`, and the conventional arrangement for binary Lua modules.
3. Add the core to `openvsm` as a native module, next to its existing `uart`
   and VDM modules. Cleanest technically, but the core stops being standalone.

### 11.3 Debug view

Proteus offers register and disassembly views for CPU-like models, and
`openvsm` already exposes that through its **VDM (Virtual Debug Monitor) Lua
bridge** — so this is a device-script feature, not a core feature. The core
supplies the state (section 8) and `zasm` supplies the disassembly, which is
already in this repository and already round-trips a 16K ROM byte-exactly.

### 11.4 Toolchain

`openvsm` builds only with 32-bit MSVC and enforces it. `z80core` is plain C
with a C ABI, which is far less demanding: MinGW and MSVC produce interoperable
C DLLs, so the core does not inherit that constraint. Phase 0 settles which
compiler actually produces a module `openvsm`'s interpreter will load, and
whether the answer differs between the two.

---

## 12. Delivery plan

Each phase ends with something testable. No phase begins before its predecessor
is green.

| Phase | Deliverable | Exit criteria |
| --- | --- | --- |
| **0. Walking skeleton** | A stub `z80core.dll` that does nothing but count ticks, loaded by `require` from a device script, on a schematic in Proteus; throughput measured through the whole stack | Proteus runs the schematic and the stub counts edges; **the linkage question of 11.2 is answered**; we know the simulator's ceiling and the cost of a round trip |
| **1. Core skeleton** | Pin struct, edge-stepped engine, changed mask, `NOP` and `HALT` only, snapshot, benchmark, C and Lua APIs | `z80_tick()` runs; ticks/s reported in CI for x86 and x86-64; core cost known as a fraction of Phase 0's ceiling |
| **2. Bus cycles** | M1, memory read/write, I/O read/write, `WAIT`, `RFSH`, `BUSRQ`/`BUSAK`, each verified edge by edge | A device script fetches and executes `NOP`s from schematic memory; every edge in 5.3 asserted by test |
| **3. Documented instruction set** | Table plus steps for all documented opcodes, all prefixes | FUSE passes for documented opcodes; ZEXDOC passes |
| **4. Undocumented set and quirks** | Undocumented opcodes, `WZ`, `Q`, `XF`/`YF`, block-instruction flags | ZEXALL and z80test pass; FUSE passes in full |
| **5. Interrupts and reset** | `INT` modes 0/1/2, `NMI`, `EI` delay, `HALT` wake, `RESET` timing | Interrupt tests pass; FUSE and SingleStepTests pass in full |
| **6. The device script** | `z80_device.lua`: pin declarations, propagation delays, memory and I/O wiring, VDM debug view via `zasm` | A real schematic runs in Proteus — Z80 plus ROM plus RAM executing code built by `zasm` |
| **7. Release** | Installer or drop-in package, examples, documentation | Someone else can place the part on a schematic and run a program without building anything |

### Where this actually stands

Kept honest deliberately, because "the code is written" and "the phase is
green" are different claims and the table above only grants the second one for
the second reason.

| Phase | State |
| --- | --- |
| 0 | **Blocked on hardware we do not have here.** Needs Proteus and the VSM SDK; the questions it must answer are below. |
| 1 | **Done.** |
| 2 | **Done bar `BUSRQ`/`BUSAK`**, which has no natural home until Phase 5 wires up the other asynchronous inputs. I/O cycles arrived with `IN` and `OUT` in Phase 3. |
| 3 | **Written, not certified.** Every encoding is implemented — 1280 of them across the base page and the CB, DD, ED, FD, DDCB and FDCB tables — and covered by results, flags and T-state counts in `test_instructions.c`. Neither FUSE nor ZEXDOC is in the repository, so the exit criteria are unmet. |
| 4 | Not started. `Q` is the known gap: `SCF` and `CCF` take `XF`/`YF` from `A` alone, where the real part ORs in the last flag-setting instruction's output. |
| 5–7 | Not started. |

Two things were pulled forward from Phase 4 rather than deferred, because each
sits inside an otherwise regular block where excluding it costs more code than
including it, and deferring it would have meant testing the same instructions
twice: **`SLL`**, at index 6 of the eight CB shifts, and the **register copy in
`DDCB`**, whose low three bits still name a register. The undocumented `XF`/`YF`
bits are implemented throughout for the same reason.

**Getting a conformance suite in is the next job, and it comes before Phase 4.**
Our own tests say the core does what we think it should; they cannot say what we
have not thought of. FUSE is the one to take first — it is small, it is a plain
text format, and alone among the suites it checks bus activity per T-state,
which is the property this core exists for.

### Why Phase 0 exists, and what it must answer

The original plan reached Proteus last. That puts every unknown at the end, and
the unknowns here are not small:

1. **Can `openvsm`'s embedded interpreter load a binary module at all?** The
   whole architecture assumes it can (11.2). If it cannot, the answer is one of
   the three fallbacks, and all of them are cheaper to adopt now than after the
   instruction set exists.
2. **Which compiler produces a module it will load?** MinGW keeps the project
   free of MSVC; MSVC is what `openvsm` itself demands. A C ABI should make
   either work, and "should" is not "does".
3. **What does the round trip cost?** Two boundary crossings per edge, at up to
   7 M edges per second. If that dominates, the tuning belongs in the device
   script, not the core.

Phase 0 is the thinnest slice through the whole stack, with a counter where the
CPU will go. It should take days. If it takes weeks, that is the single most
valuable thing this project could learn in its first month.

The risk profile after that is unchanged: phases 3 and 4 are the bulk of the
work but largely mechanical once the engine and bus cycles are right.

---

## 13. Decided, and still open

Settled:

- **Edges, not T-states** (D2, 5.3), confirmed by the VSM target: Proteus drives
  models from pin transitions.
- **The host supplies the clock level** (4.2), because `CLK` is a net that can
  be stopped or stepped.
- **The core is C, shipped as `z80core.dll`; Lua is for the device script,
  tooling and tests** (D1).
- **Lua 5.4 only** wherever Lua appears (D4). No LuaJIT, so no dialect shim and
  no FFI-versus-table question — that section of the spec is simply gone.
- **`openvsm` is the VSM layer and is not modified** (11.2). The device script
  addresses the core DLL and owns every VSM interaction.
- **x86 and x86-64 both ship.** 32-bit is Proteus's constraint, not the core's.
- **The core lives in `src/emucore/`**, beside `zasm`, which supplies the
  disassembler for the debug view.

Open:

1. **Can `openvsm`'s interpreter load a binary module, and built with what?**
   The architecture depends on it; Phase 0 answers it; 11.2 lists the
   fallbacks. This is the only open question that can change the shape of the
   project.
2. **SingleStepTests in CI.** Gigabytes. Nightly, sampled per commit, or an
   external checkout? Needed by Phase 5.
3. **The existing C++ sketch.** `src/emucore/cpu.hpp` and `cpu.cc` model pins
   and M-states in C++ and are not built. The pin layout is a useful starting
   point for `z80_pins_t`; the `vcpu` class and its `cached_ram` are superseded
   by this design. Keep as reference until Phase 1 lands, then remove.
4. **A Lua reference model.** The same instruction table could drive a slow Lua
   implementation, cross-checked against the C core by the conformance suite.
   Real value for investigation, real cost to maintain. Decide after Phase 3,
   when the table's shape is known.
