# Z80 CPU core — specification

Status: **draft for review**. Nothing is implemented yet.

This describes `z80core`, a self-contained, externally clocked, clock-precise
Z80 CPU model written in Lua. It is the CPU and nothing else: no memory, no
devices, no video, no sound, no scheduler. A host drives its clock, reads its
pins, and answers its bus requests. Put a memory map and a GPU kernel beside it
and you have a virtual machine; on its own it is a chip.

---

## 1. Scope

### 1.1 In scope

- The complete Z80 instruction set: documented and undocumented.
- Complete internal state, including the parts software can observe only
  indirectly (`WZ`/MEMPTR, `Q`, `IFF1`/`IFF2`, interrupt mode, refresh).
- Bus and control pins, driven and sampled with correct timing.
- Interrupt (`INT`), non-maskable interrupt (`NMI`), `RESET`, `WAIT`,
  `BUSRQ`/`BUSAK`, `HALT`, `RFSH`.
- Cycle-exact behaviour at T-state granularity, including the internal
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

### 1.3 What "done" means

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

### D2 — Pin-level, one call per T-state

`tick()` advances the CPU by exactly one T-state. The host observes and drives
pins between ticks. This mirrors the real part, makes `WAIT` states and machine
specific contention a host concern rather than a core concern, and is the
interface a VM wants when a GPU kernel shares the same clock.

Consequence: this is the slowest possible interface, one cross-boundary
interaction per clock. Section 10 makes the cost budget explicit.

### D3 — Everything, undocumented included

Undocumented opcodes, `WZ`/MEMPTR, `Q`, `XF`/`YF`, the block-instruction flag
oddities, exact interrupt acceptance timing. `zasm` already assembles the
undocumented set; a core that cannot run what the assembler emits would be a
strange pairing.

### D4 — LuaJIT, real-time required

The core must run a 3.5 MHz Z80 in real time on a desktop CPU. That is
3.5 million `tick()` calls per second plus the host's work per tick.

Consequence, and the one place D1 and D4 pull against each other: the fast path
wants LuaJIT's FFI (flat C structs, no hashing, no boxing), and FFI is not
portable Lua. The resolution is in [section 4.4](#44-two-representations-one-semantics):
an FFI representation and a plain-table representation, selected at load time,
with identical observable semantics and one shared test suite. Portability is
preserved; speed is available where it matters.

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
cpu:tick()      -- advance exactly one T-state
```

`tick()` reads the input pins, advances the machine by one T-state, and updates
the output pins. It never calls into the host, never allocates, and never
yields. Everything the host needs to know is in the pins afterwards; everything
the core needs to know the host puts in the pins beforehand.

A host loop looks like this:

```lua
while running do
  -- 1. present inputs
  cpu.pins.INT = irq_line

  -- 2. advance one clock
  cpu:tick()

  -- 3. answer the bus
  local p = cpu.pins
  if p.MREQ and p.RD then
    p.D = memory[p.A]
  elseif p.MREQ and p.WR then
    memory[p.A] = p.D
  elseif p.IORQ and not p.M1 then
    ...
  end
end
```

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

---

## 5. Execution model

### 5.1 State machine, not an interpreter loop

The core holds a **step cursor**. Each instruction is a list of steps, one per
T-state. `tick()` executes the step at the cursor and advances it. There is no
"execute instruction" function, because an instruction never executes at a
single moment: it is spread across its T-states, and the bus activity is what
the outside world sees.

```
step list for LD A,(HL)   -- 7 T-states: M1 of 4, memory read of 3
  1  M1 T1: A <- PC, MREQ+RD+M1 asserted
  2  M1 T2: sample WAIT; sample D at the end
  3  M1 T3: latch opcode, R refresh begins, A <- I:R, RFSH
  4  M1 T4: decode, RFSH ends
  5  MR T1: A <- HL, MREQ+RD
  6  MR T2: sample WAIT
  7  MR T3: A_reg <- D
```

Step lists are built once, at load time, from the instruction table
(section 5.4). At run time the core indexes an array and calls a function; it
never parses, allocates, or branches on a description.

### 5.2 WAIT

`WAIT` is sampled at the defined point of the cycles that can be stretched
(M1 T2, and T2 of memory and I/O cycles). If asserted, the cursor does not
advance: the core repeats a wait T-state, holding its pins, until `WAIT` is
released.

This is the whole contention mechanism. The core knows nothing about the ZX
Spectrum's ULA or any other machine's timing; a host that wants contention
asserts `WAIT`, and the timing that results is correct by construction.

### 5.3 Bus cycle timing

The canonical cycles, with the point at which each side acts:

| Cycle | T-states | Core drives | Host must respond |
| --- | --- | --- | --- |
| M1 (opcode fetch) | 4 | `A`=PC, `M1`+`MREQ`+`RD` in T1; `RFSH`+`A`=I:R in T3 | put opcode on `D` by end of T2 |
| Memory read | 3 | `A`, `MREQ`+`RD` in T1 | put data on `D` by end of T2 |
| Memory write | 3 | `A`, `D`, `MREQ` in T1, `WR` in T2 | latch `D` on `WR` |
| I/O read | 4 | `A`, `IORQ`+`RD` in T2 | put data on `D` by end of T3 |
| I/O write | 4 | `A`, `D`, `IORQ`+`WR` in T2 | latch `D` |
| Interrupt acknowledge | 6 | `M1`+`IORQ` | put vector/opcode on `D` |
| Internal | 1..n | nothing asserted | nothing |

I/O cycles are one T-state longer than memory cycles: the Z80 inserts a wait
state automatically, which is why `IN`/`OUT` take 11 or 12 T-states rather
than 10.

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

Real time for a 3.5 MHz Z80: **3.5 million ticks per second sustained**, with
headroom for the host's per-tick work. A useful internal goal is 10× that in
isolation, so that a full machine still fits in real time.

### 10.2 Budget

At 3.5 MHz on a 3.5 GHz core there are ~1000 CPU cycles per T-state for
everything: core, host memory access, and device emulation. That is a great
deal of room, and it is entirely possible to spend it all on interpreter
overhead. Hence the rules below.

### 10.3 Rules for the hot path

1. No allocation per tick. Ever. Step lists, pin storage and state are created
   once.
2. No closure creation per tick; step functions are created at load time.
3. No string keys in the tick path on the LuaJIT path — FFI struct fields only.
4. No varargs, no `pcall`, no metamethod dispatch inside `tick()`.
5. Event dispatch is chosen when a listener is registered, not tested per tick.
6. One monomorphic call shape for step functions, so the JIT can inline.

### 10.4 Measurement

A benchmark harness lands in Phase 1, before the instruction set is filled in,
and reports ticks/second in CI. If the design cannot hit the target on a
skeleton core, that is the moment to find out — not after 1270 encodings are
written against it.

---

## 11. Packaging

### 11.1 As a Lua module

`require("z80core")`, pure Lua, no build step, works with LuaJIT or stock Lua
5.4 (section 4.4).

### 11.2 As a C library

Since the Lua core is the product, the C library embeds a Lua runtime:

```c
z80_t   *cpu = z80_new();
z80_pins_t pins = {0};
for (;;) {
    z80_tick(cpu, &pins);
    if (pins.mreq && pins.rd) pins.d = mem[pins.a];
}
```

- One static library: interpreter + core (as precompiled bytecode) + the shim.
- The C struct and the Lua pin table describe the same bits, and a test asserts
  the two agree.
- Reality check on "everywhere": LuaJIT covers x86, x86-64, ARM, ARM64,
  PPC and MIPS, but not every platform stock Lua reaches, and some platforms
  forbid JIT compilation. The portable path exists for those, at a speed cost.
  This should be stated in the README rather than discovered by a user.

---

## 12. Delivery plan

Each phase ends with something testable. No phase begins before its predecessor
is green.

| Phase | Deliverable | Exit criteria |
| --- | --- | --- |
| **1. Skeleton and harness** | Pin bundle, both representations, step engine, tick loop, `NOP` and `HALT` only, benchmark, snapshot | `tick()` runs; benchmark reports ticks/s; both representations agree; ticks/s target met on the skeleton |
| **2. Bus cycles** | M1, memory read/write, I/O read/write, `WAIT`, `RFSH`, `BUSRQ`/`BUSAK` | A host can fetch and execute `NOP`s from its own memory; `WAIT` stretches cycles correctly; internal timing tests pass |
| **3. Documented instruction set** | Table plus steps for all documented opcodes, all prefixes | FUSE passes for documented opcodes; ZEXDOC passes |
| **4. Undocumented set and quirks** | Undocumented opcodes, `WZ`, `Q`, `XF`/`YF`, block-instruction flags | ZEXALL and z80test pass; FUSE passes in full |
| **5. Interrupts and reset** | `INT` modes 0/1/2, `NMI`, `EI` delay, `HALT` wake, `RESET` timing | Interrupt tests pass; FUSE and SingleStepTests pass in full |
| **6. Packaging** | C shim, static library, README, examples | C example runs a program against host memory; CI builds and tests it |

An honest note on sequencing: phases 3 and 4 are the bulk of the work, and they
are mostly mechanical once phases 1 and 2 are right. The risk is concentrated
in phase 1 — if the tick interface is wrong, everything after it is wrong.
That is why the benchmark and the equivalence test come first.

---

## 13. Open questions

1. **Half-cycle ticks.** The real chip acts on both clock edges: `WAIT` is
   sampled on a falling edge, some outputs change on rising. This spec models a
   full T-state per tick and defines the sampling point inside it. That is
   enough for every test suite in section 9 and for machine contention. If you
   later want to model bus timing at edge resolution (rare outside hardware
   co-simulation), it would mean doubling the tick rate and revisiting
   section 5.3.
2. **Which Lua does the C library ship?** LuaJIT for speed, stock 5.4 for
   reach, or both as build options. This can be deferred to phase 6, but not
   past it.
3. **SingleStepTests in CI.** The full set is gigabytes. Nightly job, a fixed
   sample per commit, or an external checkout?
4. **Where does the core live?** A `lua/` directory in this repository beside
   `zasm`, or its own repository with this one as a consumer? The shared
   instruction-set knowledge (section 5.4) argues for keeping them together.
