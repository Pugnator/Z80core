# A language above assembly

An idea, not a plan. Recorded so the decision is made deliberately rather than
by whichever thing gets written first.

An assembler and a disassembler make a toolkit. An SDK is what you get when
somebody can write a program without counting T-states — so the question is
what the rung above assembly should be, and there are more candidates than the
obvious one.

---

## 1. Three different things get called "adding a language"

Worth separating before choosing, because they solve different problems and
have wildly different costs.

| | Runs where | What it buys | Cost |
| --- | --- | --- | --- |
| **A macro layer** over assembly | build time, on the PC | conditionals, repetition, structures, named constants — assembly without the tedium | small |
| **A host language** driving the tools | on the PC | scripted builds, generated tables, test harnesses | already done |
| **A target language** compiled or interpreted for the Z80 | on the Z80 | programs written in something other than assembly | large |

This project already has the middle one: Lua drives the tests, the openvsm
device script, and `macro/preprocessor.lua` — a sketch of the first one from
years ago that never grew past `#define`.

The README's old note, *"Lua to Forth translator"*, sits between two of these,
which is a sign the distinction was never quite drawn. It is drawn now — and
Lua appears in section 4 as a target language in its own right, where the same
three-way confusion has to be untangled again.

---

## 2. Why not simply reuse an existing compiler

Because for this target the usual answer is not available.

- **GCC** has no maintained Z80 back end, and its internals assume a machine
  the Z80 is not: plenty of general-purpose registers, cheap stack-relative
  addressing, orthogonal operations. (GCC *does* target 8-bit AVR — but AVR
  has 32 registers and a regular instruction set, which is exactly the
  difference.) Z80 back ends have been attempted and abandoned more than once.
- **LLVM** has experimental out-of-tree Z80 back ends. None are production
  tools.
- **SDCC** is the real answer in the wild: a purpose-built retargetable
  compiler for 8-bit parts, Z80 included, and **z88dk** wraps it into a
  complete retro toolchain.

So the honest framing is: a C compiler for the Z80 *exists and works*. Writing
one here would be for ownership, for integration with this project's tools, and
for the pleasure of it — not because a gap needs filling. Those are legitimate
reasons in a project like this one; they are just not the same as necessity,
and it is better to know which reason applies.

An alternative worth weighing: **make `zasm` a good citizen for SDCC output**
instead. That is a much smaller job — mostly directive and syntax
compatibility — and it yields a working C toolchain immediately.

---

## 3. The lever that makes any of this cheap

Whatever the language, **emit assembly text and let `zasm` assemble it.**

That is not a shortcut, it is the reason a compiler here is tractable:

- 1270 encodings are already implemented and verified.
- The output is *readable*, so a codegen bug is visible rather than deduced
  from bad bytes.
- `zasm` already reproduces a real 16K ROM byte-exactly and round-trips its own
  disassembly, so the back end is trustworthy in a way a fresh one would not be
  for a long time.
- The compiler needs no object format, no linker, no relocation — at first.

A compiler that emits text can be written, tested and debugged with the tools
in this repository today. One that emits bytes needs a second implementation of
everything `zasm` already does.

---

## 4. The candidates

### Forth

The traditional answer for exactly this niche, and the smallest thing that is
genuinely a language rather than a macro layer.

- **Interactive on the target.** A REPL on a simulated Z80, in `z80mon` or in
  Proteus, is a genuinely different experience from edit-assemble-run — and it
  is the one thing on this list that this project is unusually well placed to
  deliver, because the machine is already there.
- **No parser generator.** Forth's syntax is "read a whitespace-delimited word,
  look it up". bison and flex would contribute nothing; the compiler *is* the
  runtime, a dictionary and an interpreter loop.
- **Grows incrementally.** A dozen primitives in assembly and an inner
  interpreter give you something that already runs; everything above that is
  written in Forth itself.
- **Fits.** A usable Forth is 8–16 KB of ROM, which matters on a 64 K machine.

Against it: threaded code is slower than compiled output, and Forth asks the
programmer to think in a stack idiom that not everyone wants.

### A C subset

- **bison and flex are exactly right**, and match this project's existing
  toolchain and style — the argument for compatibility is real here.
- Familiar to everyone; the reason to want a "higher-level language" at all is
  usually "I want C".
- Well-trodden: Small-C and its descendants show the shape of a subset that is
  achievable — no floats, no `long`, no bitfields, one storage class, simple
  expressions.

Against it: the Z80 is a *poor* C target. No cheap frame-pointer-relative
addressing, one real accumulator, 8-bit ALU against 16-bit `int`. Getting from
"it compiles" to "the output is not embarrassing" is most of the work, and it is
the part a subset does not shrink. And SDCC has already done it.

### A Lua subset

Three different proposals get called "Lua on the Z80", and only the third is
worth anything. They are worth separating, because the first two are what the
idea sounds like and the third is what it should be.

**Porting the interpreter** is not a close call. Lua 5.4's number model alone
settles it: every value is a 64-bit integer *or* a double, so `a + b` is either
eight `ADC`s or a software float routine. Above that sit an interned string
table, a garbage collector, and tables carrying both an array and a hash part.
eLua — the project whose entire purpose is Lua on microcontrollers — puts its
floor near 256 KB of flash and 64 KB of RAM, on 32-bit parts with hardware
multiply. That is four times this machine's whole address space, spent before
the program exists.

**Translating VM opcodes into Z80 instructions** is feasible, and is the trap. A
Lua opcode is not a machine operation, it is a call into a runtime: `OP_ADD A B
C` means load two tagged values, branch on their tags, integer-add or float-add
or coerce, and failing all of that look up an `__add` metamethod and possibly
perform a full call. There is no Z80 sequence for that — there is a `CALL
rt_add`. So the output is a ribbon of calls into a runtime that still has to be
written in full: every cost above is kept, and one is added, because a four-byte
bytecode instruction becomes ten-odd bytes of call setup. On a 64 K machine that
trades away code density — the one thing the bytecode was good at — and buys
nearly nothing, since the time was never in the dispatch loop but inside the
helpers, and those did not change. The speedup in a real Lua JIT comes entirely
from type specialisation: proving that this particular add is always two
integers, then emitting an actual `ADD HL,DE`. That machinery is larger than
everything in this repository put together.

**Compiling a statically-typed subset ahead of time** is the real candidate, and
it is the C-subset proposal wearing better syntax. Restrict to 16-bit integers,
fixed-shape tables, no metatables, no closures over mutable upvalues, no
collector, strings as byte arrays. Then

```lua
local function checksum(addr, len)
  local sum = 0
  for i = 0, len - 1 do
    sum = sum + peek(addr + i)
  end
  return sum & 0xFFFF
end
```

compiles to respectable Z80: slots at `IX+d`, the count in `B` driving `DJNZ`,
the accumulator in `HL`.

**Compile the AST, not `luac` output.** The temptation is the same instinct as
section 3 — let `luac` do the lexing, parsing, scope resolution and constant
folding, and start from something already register-based and three-address, far
closer to a machine than JVM- or CPython-style stack bytecode. It does not pay
off, twice over:

- `luac` allocates registers for a machine that has 255 of them. This one has
  seven, eight bits wide. That work is discarded whatever happens.
- `luac` erases exactly what a typed subset needs. The compiler lives or dies on
  knowing that `sum` is an integer; `local sum = 0` says so and a compiled frame
  slot does not, because Lua reuses slots freely for different types within one
  function. Starting from bytecode means recovering by analysis what was thrown
  away one step earlier.

In its favour: Lua's grammar is about a page of EBNF, with no declarator syntax,
no preprocessor and no typedef ambiguity. **As a front end it is markedly
cheaper than C**, which is a real argument given that front-end work is most of
what a subset compiler is. Against it: C's semantics already sit near the
machine, so a C subset mostly *omits* things, while a Lua subset has to
*contradict* them — and "why can't I use a table as a table" is a worse
conversation to have with a user than "why is there no `double`".

### A Pascal subset

Rarely considered and arguably the best engineering fit: designed for
single-pass compilation, no pointer arithmetic to speak of, strict enough that
codegen stays simple. bison and flex suit it as well as they suit C.

Against it: fewer people want to write it, which matters for something billed
as an SDK.

### BASIC

Historically apt for a Spectrum, and an interpreter is easy. But as the
*SDK* language it is the weakest of the four — it would be a demo of the
machine rather than a tool for building on it.

---

## 5. Pallene, SDCC and zasm: the route with no compiler in it

Worth its own section, because it is the only path on this page that reaches a
working high-level toolchain without anybody writing a code generator.

**Pallene** is a statically-typed sister language to Lua from Roberto
Ierusalimschy's own group — the people who wrote Lua. Lua's syntax plus type
annotations, designed from the start to be compiled ahead of time rather than
interpreted. It is very nearly the subset described above, already designed and
defended by the people best placed to do it, which is worth reading before
anyone here writes a parser. It emits **C**.

**SDCC** compiles C to Z80. **zasm** assembles Z80 byte-exactly, with 1270
encodings verified against a real ROM.

So the chain exists on paper:

```
program.pln → Pallene → C → SDCC → assembly → zasm → binary
```

and the only missing arrow is the last one: `zasm` accepting SDCC's assembly
syntax and directives, which section 2 already put at days of work rather than
months. Nothing else in the chain has to be built at all.

**What is honestly wrong with it.** Pallene is not a standalone language. Its
typed sections compile to plain C operations, but anything dynamic — tables,
strings, `any` — calls into the Lua runtime, `lua_State` and all. That runtime
is precisely what section 4 ruled out for this machine. The chain therefore runs
only for the fully-typed part of Pallene, and the moment a program reaches for a
real table it wants a hundred kilobytes that do not exist here.

That may still be enough: Pallene restricted to scalars, arrays and functions is
a usable systems language, and the restriction is checkable at compile time
rather than discovered at run time. But it is a subset of a subset, and it
should be measured rather than believed.

**The experiment that settles it** is an afternoon's work: take a small
fully-typed Pallene program, read the C it emits, and count how much of it
touches `lua_State`. If the typed paths are clean C, the route is real. If the
runtime is threaded through everything, it is not, and the C subset in section 4
is back to being the honest answer.

Do the same measurement for plain SDCC output while you are there. `zasm`
compatibility is the shared prerequisite for both, and it is worth having on its
own merits whichever way the Pallene question falls.

---

## 6. What I would do

**Start with the macro layer, then Forth.** Reasons, in order:

1. **The macro layer is the best value in the project**, and Lua is already the
   obvious way to build it. Conditionals, repeat blocks, local labels,
   structures and named constants remove most of the pain of writing assembly,
   cost a fraction of a compiler, and improve every program written from now on
   including the ones in `tests/`. Note what this is: *full* Lua, on the host,
   at build time, emitting assembly text — tables, string formatting, loops that
   unroll code, computed jump tables — with nothing but the generated assembly
   ever reaching the Z80. None of section 4's difficulties apply, because no
   part of Lua runs on the target. Lua is already in the toolchain, `zasm` is
   already a library, and `macro/preprocessor.lua` is a stalled sketch of
   exactly this. Issue #6 has been asking for it since 2018.
2. **Forth suits what this project is becoming.** The core is edge-precise, the
   monitor already shows registers and memory, and Proteus puts the machine on
   a schematic. A Forth running on that, interactively, is a coherent product —
   and it needs no new front-end technology.
3. **Measure the Pallene and SDCC routes before writing any parser.** Section 5
   is an afternoon that could remove months of work, or rule the idea out
   cheaply. Either outcome is worth more than the afternoon costs, and `zasm`
   compatibility with SDCC's assembly is worth having regardless of the answer.
4. **A C or Lua subset is the biggest job with the least differentiation**,
   because SDCC exists and is good. Between the two, Lua has the cheaper front
   end and C has the smaller gap between what people expect and what the machine
   can do. If the target language must be familiar to newcomers, that gap
   matters more than the parser does.

If the goal is the *pleasure of building a compiler*, promote 4 above 2 — that is
a perfectly good reason, and a typed subset is the more interesting compiler to
write. The point of this document is that the choice should be made on that
basis rather than by accident.

---

## 7. Questions that would settle it

1. **Who is the SDK for?** Someone writing programs on a PC for a Z80 board, or
   someone poking at a simulated machine interactively? The first says a
   compiled subset, C or Lua; the second says Forth.
2. **Is target-side interactivity interesting?** It is the one thing here that
   would be distinctive, given the core and the monitor already exist.
3. **Ownership or availability?** If a working C compiler is the goal, SDCC
   already is one, and meeting it halfway is days of work rather than months.
4. **Does anything have to run on the real hardware**, or is the simulated
   machine the target? A Forth ROM implies real constraints; a cross-compiler
   does not.
5. **If Lua appeals, is it for the syntax or for the toolchain?** They point in
   opposite directions. Wanting the syntax means the compiled subset in section
   4, and a long argument with users about what was removed. Wanting Lua because
   it is already here means the macro layer in section 6, which is far cheaper
   and available now — and which is not really the same wish.

---

## 8. If Forth is chosen, the first slice

Small enough to be worth naming, so the idea has a concrete first step:

- an inner interpreter and a dictionary in Z80 assembly, assembled by `zasm`;
- about a dozen primitives — `DUP DROP SWAP + - @ ! EMIT KEY BRANCH ?BRANCH
  EXIT`;
- an outer interpreter that reads a word, looks it up, and executes or compiles
  it;
- the rest written in Forth, loaded as source.

It runs in `z80mon` the day the primitives work, which is the point: every step
is visible on hardware that already exists in this repository.
