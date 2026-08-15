# Z80 CPU core — specification

Status: **draft for review**. Nothing is implemented yet.

This describes `z80core`, a self-contained, externally clocked, edge-precise
Z80 CPU model written in Lua, living in `src/emucore/lua/`. It is the CPU and
nothing else: no memory, no devices, no video, no sound, no scheduler. A host
drives its clock one edge at a time, reads its pins, and answers its bus
requests. Put a memory map and a GPU kernel beside it and you have a virtual
machine; on its own it is a chip.

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
- **`tick()` reports which pins changed** (4.5). Useful to any event-driven
  host, and merely harmless to one that ignores it.

So Proteus sharpened the interface rather than narrowing it. What VSM gives us,
and what it demands:

| Proteus VSM | Consequence for this core |
| --- | --- |
| The simulator is event-driven; models are woken when pins change | The core must be a pure state machine with no internal clock, which is what D2 already requires |
| `CLK` is a real net driven by the schematic — it can be slowed, gated, or stopped, and the user can single-step it | The core must follow the **actual clock level** rather than assume alternating edges (section 4.2) |
| Outputs are *scheduled* at an absolute simulation time, with propagation delay | The shim needs to know **which pins changed on this edge**, not diff all of them (section 4.5) |
| Models are C++ DLLs against the VSM SDK interfaces | The C shim of section 11.2 has a concrete first consumer, not a hypothetical one |
| `ICPUMODEL` offers register and disassembly views in the debugger | The debug API of section 8 feeds it, and `zasm` already disassembles (section 11.3) |
| Simulation is not real time and never claims to be | The performance target is *usable simulation speed*, not a hard deadline (section 10.1) |

Everything the model needs beyond the CPU — memory, ROM contents, peripherals —
is on the schematic, where it belongs. The core stays a chip.

### 1.4 What "done" means

The core is finished when it passes every suite in [section 9](#9-conformance)
with no exemptions, and sustains the throughput target in
[section 10](#10-performance).

---

## 2. Design decisions

These were settled before writing this spec; they are recorded with their
consequences so the reasoning survives.

### D1 — The Lua core is the product

There is one implementation, in Lua. A C host embeds it. There is no second
engine to keep in sync and no generated C to review, so behaviour cannot drift
between implementations.

Consequence: "runs virtually everywhere" means "everywhere the chosen Lua
runtime runs". Shipping a C library therefore means shipping the interpreter
inside it — see [section 11](#11-packaging).

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

### D4 — LuaJIT on the PC, fast enough to be pleasant

The core targets LuaJIT on a desktop, inside a Proteus VSM model. The number to
design against is 7 million `tick()` calls per second — real time for a 3.5 MHz
Z80 — with the nuance in [section 10.1](#101-target): simulation speed is a
quality bar, not a deadline, and correctness is never traded for it.

This decision is bounded by where the core actually runs. It runs on the PC.
LuaJIT cannot execute on Cortex-M — its ARM backend emits A32 instructions and
Cortex-M is Thumb-2 only — so a microcontroller build is not a stretch goal of
this design but a different engine driven by the same instruction table, to be
entered deliberately if it is ever wanted.

Consequence, and the one place D1 and D4 pull against each other: the fast path
wants LuaJIT's FFI (flat C structs, no hashing, no boxing), and FFI is not
portable Lua. Section 4.4 keeps both representations behind identical
semantics — but the FFI path should be adopted only if Phase 1 measurement
shows plain flat arrays cannot reach the target, since one representation is
worth more than a speculative optimisation.

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

### 4.1 Construction

```lua
local z80 = require("z80core")

local cpu = z80.new()          -- state after power-on, see 7.1
```

The constructor takes no host callbacks, no memory, no configuration. A CPU is
a CPU.

### 4.2 The tick

```lua
cpu:tick()      -- advance exactly one clock edge (half a T-state)
```

`tick()` reads the input pins, advances the machine to the next clock edge, and
updates the output pins. It never calls into the host, never allocates, and
never yields. Everything the host needs to know is in the pins afterwards;
everything the core needs to know the host puts in the pins beforehand.

Two ticks make one T-state. The host passes the **new clock level**, and the
core advances on the transition:

```lua
cpu:tick(1)     -- rising edge
cpu:tick(0)     -- falling edge
```

Calling `tick()` with the level the core is already at is a no-op, not an
error: a clock that is stopped, gated or stepped by hand simply does not
advance the CPU, which is exactly what the hardware does.

An earlier draft had the core own the clock phase and alternate edges by
itself, on the grounds that a host-driven level costs a comparison on the
hottest path. Proteus settles it the other way: `CLK` is a real net on the
schematic. It can be halted, single-stepped, run at 1 Hz for debugging, or
driven by something other than a clean oscillator, and a core that assumed
alternating edges would silently desynchronise from the net it is wired to. One
comparison per tick is a small price for following the actual clock.

A host loop looks like this:

```lua
while running do
  -- 1. present inputs
  cpu.pins.INT = irq_line

  -- 2. advance one clock edge
  cpu:tick()

  -- 3. answer the bus
  local p = cpu.pins
  if p.MREQ and p.RD then
    p.D = memory[p.A]         -- must be valid before the sampling edge
  elseif p.MREQ and p.WR then
    memory[p.A] = p.D         -- latch while WR is asserted
  elseif p.IORQ and not p.M1 then
    ...
  end
end
```

A host that models nothing sub-cycle can read and write on any edge where the
strobe is asserted, because the core holds its bus signals for the whole of
their defined window. A host that does care — a ULA, a GPU kernel sharing the
bus — has every edge available to it.

### 4.3 Events

"Event-driven" here means the core is a state machine that moves only when
clocked, and everything it communicates is a pin state change. On top of that,
the core can emit *notifications* for hosts and debuggers that would otherwise
have to reconstruct them by watching pins:

```lua
cpu:on("m1",        function(pc, opcode) ... end)   -- instruction fetch
cpu:on("interrupt", function(mode, vector) ... end) -- INT/NMI accepted
cpu:on("halt",      function(pc) ... end)
cpu:on("reset",     function() ... end)
```

Rules that keep this honest:

1. Notifications are **derived**, never authoritative. A host that ignores them
   entirely sees identical behaviour.
2. With no listener registered, the hot path must not test for one per tick;
   dispatch is switched at registration time (section 10.3).
3. Handlers must not mutate CPU state. The debugging API in section 8 is the
   supported way to do that.

### 4.4 Two representations, one semantics

| | LuaJIT path | Portable path |
| --- | --- | --- |
| State and pins | FFI `struct`, fixed-width integer fields | plain Lua tables, integer values |
| Selection | automatic at load, when `jit` and `ffi` are present | fallback |
| Observable behaviour | identical | identical |
| Test suite | the same one runs against both | the same one runs against both |

`cpu.pins.A` reads the same in both; only the storage differs. Any behavioural
difference between the two paths is a bug, and CI runs the conformance suite on
both to keep it that way.

### 4.5 What changed on this edge

After `tick()`, `cpu.changed` is a bitmask of the output pins the core drove to
a new value on that edge, and `0` when it drove none.

This exists because of how a VSM model has to publish its outputs. Proteus does
not want "here is the state of every pin"; it wants "this pin takes this value
at this time", scheduled with the propagation delay for that signal — `MREQ`
falling has a different delay from the address bus becoming valid. Without a
mask the shim would have to diff thirty pins on every edge, at 7 M edges per
second, to recover information the core already had.

Most edges change nothing, so the common case is a single compare against zero:

```lua
cpu:tick(clk)
if cpu.changed ~= 0 then
  publish(cpu.pins, cpu.changed)   -- schedule only what moved, each with its own delay
end
```

The core exposes *what* changed. It never expresses *when* in analog time —
propagation delays belong to the model wrapping it, since they are a property
of the part being modelled (and its temperature, and its supply voltage), not
of the instruction set.

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

Step lists are built once, at load time, from the instruction table
(section 5.4). At run time the core indexes an array and calls a function; it
never parses, allocates, or branches on a description.

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
| T3 ↑ | samples `D`, releases `MREQ`/`RD`/`M1` | opcode must be valid on `D` |
| T3 ↓ | `A` ← `I:R`, `RFSH` asserted, `R` incremented | |
| T4 ↓ | `RFSH` released | |

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

The Zilog *Z80 CPU User Manual* timing diagrams are the reference for the table
above, and the conformance suites in section 9 are the arbiter for anything
they leave ambiguous. Phase 2 verifies each cycle against both before any
instruction depends on it.

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

```lua
local snap = cpu:save()     -- plain Lua table, serialisable, version tagged
cpu:load(snap)              -- exact restore, mid-instruction included
```

A snapshot captures the step cursor too, so a VM can be saved between any two
T-states, not merely between instructions. Round-tripping a snapshot must be
bit-exact, and a test asserts that saving, restoring and continuing produces
the identical trace.

---

## 8. Debug API

Separate from the hot path, and permitted to be slow:

```lua
cpu:get("HL")        cpu:set("HL", 0x8000)
cpu:registers()      -- snapshot of the register file as a table
cpu:disasm_state()   -- current opcode, prefix, M-cycle, T-state within it
```

Hosts get read/write access to everything, including `WZ` and `Q`, because a
debugger that cannot see the whole machine is a debugger that lies.

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
| **Internal** | Snapshot round-trip, `WAIT` stretching, `BUSRQ`, reset timing, EI/prefix interrupt rules, LuaJIT vs portable path equivalence. | The behaviours the public suites do not reach. |

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

On a 3.5 GHz core, 7 M ticks/s leaves **about 500 CPU cycles per tick** for
everything: the core's step, the host's bus handling, and every device on the
clock. That is a comfortable budget for a step function that indexes an array
and writes a few struct fields, and no budget at all for per-tick allocation,
hashing, or dispatch through metamethods. Hence the rules below.

The edge decision (D2) is what halved this budget. It buys timing fidelity that
a T-state model cannot express, and it is the single largest performance risk
in the project — which is why Phase 1 measures a skeleton core before the
instruction set exists. If 7 M ticks/s is not reachable, the options are known
in advance and are all worse: collapse idle edges, drop to T-state ticks with
an edge-resolved variant for the cycles that need it, or move the hot loop into
the C shim.

### 10.3 Rules for the hot path

1. No allocation per tick. Ever. Step lists, pin storage and state are created
   once.
2. No closure creation per tick; step functions are created at load time.
3. No string keys in the tick path on the LuaJIT path — FFI struct fields only.
4. No varargs, no `pcall`, no metamethod dispatch inside `tick()`.
5. Event dispatch is chosen when a listener is registered, not tested per tick.
6. One monomorphic call shape for step functions, so the JIT can inline.

### 10.4 Measurement

Two benchmarks, and both exist before the instruction set does:

1. **Through the whole stack** (Phase 0): Proteus → VSM DLL → LuaJIT → `tick()`,
   with a stub model. This gives the simulator's ceiling and the real cost of a
   round trip across the boundary. Everything else is tuning against a guess
   until this number exists.
2. **The core alone** (Phase 1): ticks/second for a skeleton CPU, reported in
   CI, on **both 32- and 64-bit LuaJIT** — both are shipped configurations, so
   both are measured. The 32-bit figure is the one to compare against Phase 0's
   ceiling, since that is where Proteus runs.

The order matters. Measuring the core in isolation first would have told us the
design was fine and taught us nothing about whether the deployment path works
at all.

---

## 11. Packaging

### 11.1 As a Lua module

`require("z80core")`, pure Lua, no build step, works with LuaJIT or stock Lua
5.4 (section 4.4).

### 11.2 As a C library

Since the Lua core is the product, the C library embeds a Lua runtime, and that
runtime is **LuaJIT**: the real-time target in section 10 is not reachable
without it, so shipping anything else would ship a core that misses its own
requirement.

```c
z80_t   *cpu = z80_new();
z80_pins_t pins = {0};
for (;;) {
    z80_tick(cpu, &pins);
    if (pins.mreq && pins.rd) pins.d = mem[pins.a];
}
```

- **Both x86 and x86-64 are shipped configurations.** The Lua core is
  bitness-agnostic; only packaging cares. 32-bit exists because Proteus needs
  it (11.3), not because the core does, and a host on either width is equally
  supported.
- One static library per configuration: interpreter + core (as precompiled
  bytecode) + the shim.
- The C struct and the Lua pin table describe the same bits, and a test asserts
  they agree **on both widths**. That is cheap to guarantee and easy to break:
  the shared layout uses fixed-width fields only, and never a pointer or a
  `long`, so 32- and 64-bit builds cannot disagree about it.
- Reality check on "everywhere": LuaJIT covers x86, x86-64, ARM, ARM64, PPC and
  MIPS — desktops and application-class ARM. It does **not** cover Cortex-M:
  LuaJIT's ARM backend emits A32 instructions and Cortex-M is Thumb-2 only, so
  no STM32 or similar part can host this core. A microcontroller target would
  need a C engine driven by the same instruction table, which is a different
  project and should be entered deliberately rather than discovered. The
  portable Lua path (section 4.4) widens reach among *hosted* platforms only.

### 11.3 As a Proteus VSM model

The shipping artifact for the primary consumer (section 1.3) is a Windows DLL:
VSM SDK interfaces on the outside, the C shim and LuaJIT inside.

- **Model type.** `IDSIMMODEL` is enough to make the part work on a schematic.
  `ICPUMODEL` additionally gives register and disassembly views inside the
  Proteus debugger, which is the interesting option here, because `zasm`
  already disassembles Z80 — the debug view can be driven by the disassembler
  in this same repository rather than a second one written for the purpose.
- **Pins.** The VSM pin API deals in state changes scheduled at absolute
  simulation times, with edge queries on inputs. The changed-pin mask of
  section 4.5 is what feeds it.
- **Bitness: 32-bit here specifically.** Proteus 8 is a 32-bit application, so
  *this model* is an x86 DLL with an x86 LuaJIT inside. That is a constraint of
  this host, not of the core, which ships for both widths (11.2). Consequences
  worth knowing up front rather than at link time:
  - The repository's existing toolchain (MSYS2 UCRT64) is x86-64 and cannot
    build it. The shim needs an i686 toolchain — MSYS2 MINGW32 or 32-bit MSVC.
  - The VSM SDK's interfaces are C++ abstract classes, and Proteus is built
    with MSVC. Crossing that vtable boundary from a MinGW-built DLL is the kind
    of thing that either works or fails in ways that cost a week, so **MSVC
    32-bit is the default choice for the shim**, with MinGW as the fallback to
    try only if MSVC proves awkward.
  - `zasm` keeps its own toolchain. The two builds are independent; only the
    disassembler source is shared, and only if `ICPUMODEL` is adopted.
  - LuaJIT's x86 backend is mature and fast, so 32-bit costs nothing in
    performance here. It does mean the figure to compare against this host's
    ceiling is the 32-bit one; both widths are measured (10.4), but a 64-bit
    number says nothing about what Proteus will do.
- **Delays.** Datasheet propagation delays live in the shim, not the core
  (section 4.5).

---

## 12. Delivery plan

Each phase ends with something testable. No phase begins before its predecessor
is green.

| Phase | Deliverable | Exit criteria |
| --- | --- | --- |
| **0. Walking skeleton** | 32-bit LuaJIT embedded in a minimal VSM DLL; a stub model with a handful of pins that toggles one of them from Lua; a schematic that loads it; throughput measured through the whole stack | Proteus loads the model and runs the schematic; Lua drives a pin; **we know the simulator's events/second ceiling and the cost of one round trip** |
| **1. Core skeleton** | Pin bundle, edge-stepped engine, changed-pin mask, `NOP` and `HALT` only, snapshot, benchmark | `tick()` runs; ticks/s reported in CI for 32- and 64-bit LuaJIT; representation decision made on measurement (4.4); core cost is small next to Phase 0's ceiling |
| **2. Bus cycles** | M1, memory read/write, I/O read/write, `WAIT`, `RFSH`, `BUSRQ`/`BUSAK`, each verified edge by edge | A host can fetch and execute `NOP`s from its own memory; every edge in 5.3 asserted by test; `WAIT` stretches correctly |
| **3. Documented instruction set** | Table plus steps for all documented opcodes, all prefixes | FUSE passes for documented opcodes; ZEXDOC passes |
| **4. Undocumented set and quirks** | Undocumented opcodes, `WZ`, `Q`, `XF`/`YF`, block-instruction flags | ZEXALL and z80test pass; FUSE passes in full |
| **5. Interrupts and reset** | `INT` modes 0/1/2, `NMI`, `EI` delay, `HALT` wake, `RESET` timing | Interrupt tests pass; FUSE and SingleStepTests pass in full |
| **6. Packaging** | Static library for x86 and x86-64, C API, README, examples | C example runs a program against host memory; CI builds and tests **both** configurations; the shared pin layout is asserted identical on both |
| **7. Proteus VSM model** | Full VSM model: all pins bound, propagation delays, optional `ICPUMODEL` debug view driven by `zasm`'s disassembler | A real schematic runs in Proteus — Z80 plus ROM plus RAM executing code built by `zasm` |

### Why Phase 0 exists

The original plan started at the core and reached Proteus last. That order puts
every unknown at the end: whether 32-bit LuaJIT embeds cleanly in a DLL,
whether MSVC or MinGW is the right compiler for the VSM interfaces, whether
Proteus loads what we build, and what the simulator's own throughput is. Any of
those could reshape the design, and finding out after 1270 encodings exist is
the expensive way to learn it.

Phase 0 is a walking skeleton: the thinnest possible slice through the entire
stack, with a stub where the CPU will go. It proves the path and produces the
one number the performance work should be aimed at. It should take days, not
weeks — and if it takes weeks, that is itself the most valuable thing we could
have learned this early.

The risk profile of the rest is unchanged: phases 3 and 4 are the bulk of the
work but largely mechanical once 1 and 2 are right, and the design risk is
concentrated in the tick interface, which is why the equivalence test and the
benchmark come before the instruction set.

---

## 13. Decided, and still open

Settled after the first review of this spec:

- **Edges, not T-states.** The core ticks per clock edge (D2, section 5.3).
  This is the decision the rest of the timing model hangs on, and the VSM
  target confirms it: Proteus drives models from pin transitions.
- **The host supplies the clock level**, the core does not own the phase
  (section 4.2), because in Proteus `CLK` is a net that can be stopped or
  stepped.
- **The first host is a Proteus VSM model on the PC** (section 1.3), and the
  core ships for **both x86 and x86-64** (section 11.2). 32-bit is Proteus's
  requirement, not the core's. LuaJIT is available on both, so the runtime
  question is closed — and running on a microcontroller is explicitly *not* a
  goal, since LuaJIT cannot execute on Cortex-M at all (section 11.2).
- **The C library ships LuaJIT** (section 11.2).
- **The core lives in `src/emucore/lua/`**, beside the existing C++ pin sketch
  rather than in a repository of its own. `zasm` and the core have to agree on
  which encodings exist (section 5.4), and a test asserts it — that is hard to
  keep true across repositories and trivial to keep true inside one.

Still open:

1. **SingleStepTests in CI.** The full set is gigabytes. Nightly job, a fixed
   sample per commit, or an external checkout? Deferred; needed by Phase 5.
2. **What happens to the existing C++ sketch.** `src/emucore/cpu.hpp` and
   `cpu.cc` model the pins and M-states in C++ and are not built today. The pin
   layout there is a good starting point for the C ABI in Phase 6, so the
   proposal is to keep it as reference until then and let the shim replace it,
   rather than deleting work that is about to be useful.
3. **`IDSIMMODEL` or `ICPUMODEL`** for the VSM model (section 11.3). The second
   gives a debugger view driven by `zasm`'s disassembler, at the cost of a
   larger interface to implement. Needed by Phase 7, not before — but Phase 0
   should stub whichever one we expect to use, so the interface is exercised
   early rather than assumed.
4. **MSVC or MinGW for the shim** (section 11.3). MSVC 32-bit is the default,
   since Proteus is MSVC-built and the VSM interfaces are C++ abstract classes.
   Phase 0 answers this by building something and seeing whether Proteus loads
   it, which is the only answer that counts.
5. **Where LuaJIT comes from.** Vendored as a submodule, pinned and built by
   our CMake, or an external prerequisite? A 32-bit build is not what a
   developer will have lying around, so pinning it is probably worth the
   repository weight. Decide in Phase 0.
