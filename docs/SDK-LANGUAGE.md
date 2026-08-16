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
which is a sign the distinction was never quite drawn. It is drawn now.

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

## 5. What I would do

**Start with the macro layer, then Forth.** Reasons, in order:

1. **The macro layer is the best value in the project.** Conditionals, repeat
   blocks, local labels, structures and named constants remove most of the pain
   of writing assembly, cost a fraction of a compiler, and improve every
   program written from now on including the ones in `tests/`. `zasm` is
   already a library, so this can live in front of it or inside it. Issue #6
   has been asking for it since 2018.
2. **Forth suits what this project is becoming.** The core is edge-precise, the
   monitor already shows registers and memory, and Proteus puts the machine on
   a schematic. A Forth running on that, interactively, is a coherent product —
   and it needs no new front-end technology.
3. **A C subset is the biggest job with the least differentiation**, because
   SDCC exists and is good. If C matters more than ownership, the cheaper path
   is `zasm` compatibility with SDCC's assembly output, and that is worth
   measuring before writing a parser.

If the goal is the *pleasure of building a compiler*, invert 2 and 3 — that is a
perfectly good reason, and the C subset is the more interesting compiler to
write. The point of this document is that the choice should be made on that
basis rather than by accident.

---

## 6. Questions that would settle it

1. **Who is the SDK for?** Someone writing programs on a PC for a Z80 board, or
   someone poking at a simulated machine interactively? The first says C, the
   second says Forth.
2. **Is target-side interactivity interesting?** It is the one thing here that
   would be distinctive, given the core and the monitor already exist.
3. **Ownership or availability?** If a working C compiler is the goal, SDCC
   already is one, and meeting it halfway is days of work rather than months.
4. **Does anything have to run on the real hardware**, or is the simulated
   machine the target? A Forth ROM implies real constraints; a cross-compiler
   does not.

---

## 7. If Forth is chosen, the first slice

Small enough to be worth naming, so the idea has a concrete first step:

- an inner interpreter and a dictionary in Z80 assembly, assembled by `zasm`;
- about a dozen primitives — `DUP DROP SWAP + - @ ! EMIT KEY BRANCH ?BRANCH
  EXIT`;
- an outer interpreter that reads a word, looks it up, and executes or compiles
  it;
- the rest written in Forth, loaded as source.

It runs in `z80mon` the day the primitives work, which is the point: every step
is visible on hardware that already exists in this repository.
