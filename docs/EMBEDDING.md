# Embedding Z80core

How to put the CPU core, the assembler and the disassembler inside your own
program, whatever that program is.

Three libraries, each usable on its own:

| Library | Header | What it needs |
| --- | --- | --- |
| `z80core` | `z80core.h` | a C99 compiler. Nothing else — no allocation after construction, no I/O, no C library beyond `string.h` and `stdlib.h` |
| `zdasm` | `zdasm.h` | a C99 compiler and `snprintf` |
| `zasmlib` | `zasm.h` | the above, plus `malloc`, `stdio`, and a temporary stream for the `tap` and `ihex` formats |

They do not depend on each other except that `zasmlib` links `zdasm`, and none
of them depend on Proteus, openvsm, Lua, or the monitor.

---

## 1. The CPU core

### What it is

A Z80 that is clocked from outside and talks through pins. It holds no memory,
no devices and no scheduler: your program owns all of that and answers the
core's bus requests. Full detail in [CPU-CORE-SPEC.md](CPU-CORE-SPEC.md).

### The smallest complete host

```c
#include "z80core.h"

static uint8_t memory[0x10000];

int main(void)
{
    z80_t *cpu = z80_new();
    z80_pins_t pins = {0};
    int clk = 0;

    for (;;)
    {
        clk = !clk;                       /* you own the clock */
        z80_tick(cpu, &pins, clk);

        if (pins.ctrl & Z80_MREQ)
        {
            if (pins.ctrl & Z80_RD) pins.D = memory[pins.A];
            if (pins.ctrl & Z80_WR) memory[pins.A] = pins.D;
        }
        else if (pins.ctrl & Z80_IORQ)
        {
            if (pins.ctrl & Z80_RD) pins.D = port_read(pins.A);
            if (pins.ctrl & Z80_WR) port_write(pins.A, pins.D);
        }
    }

    z80_free(cpu);
}
```

That is the whole contract. Everything below is detail on top of it.

### The clock is yours

`z80_tick()` takes the **new clock level**, not a request to advance. Passing
the level the core already holds does nothing, which is what lets you stop the
clock, gate it, single-step it by hand, or drive it from a schematic net that
is doing something irregular.

Two ticks make one T-state. After `z80_new()` or `z80_reset()` the clock is
**low**, so the first call that advances anything is `z80_tick(cpu, &pins, 1)`.
A loop that starts at 0 silently performs one fewer edge than it thinks.

For a host that only wants to run flat out, `z80_run()` does the same work with
one call instead of one per edge.

### Answering the bus

The core holds each bus signal for the whole of its defined window, so a simple
host can read and write on any edge where the strobe is asserted, as above. A
host that models sub-cycle behaviour — a ULA, contention, a device sharing the
bus — has every edge available and should consult the timing tables in
[CPU-CORE-SPEC.md](CPU-CORE-SPEC.md) §5.3.

Data must be valid on `pins.D` **before** the edge on which the core samples
it. Writing it immediately after the tick that asserted `RD`, as the loop above
does, satisfies that.

### Only publishing what moved

`z80_tick()` returns a mask of the output pins it drove to a new value, and `0`
when it drove none — which is most edges. An event-driven host uses it to avoid
diffing pins it already knows are unchanged:

```c
uint32_t changed = z80_tick(cpu, &pins, clk);
if (changed)
{
    if (changed & Z80_MREQ) publish_mreq(pins.ctrl & Z80_MREQ);
    if (changed & Z80_CHANGED_A) publish_address(pins.A);
}
```

The core says *what* changed. *When* it changed in analog time — propagation
delay — belongs to your model, because it is a property of the part you are
modelling and its conditions, not of the instruction set.

### Building it

```cmake
add_subdirectory(path/to/Z80core/src/emucore/z80core)
target_link_libraries(your_target PRIVATE z80core)
```

Or copy two files — `z80core.c` and `z80core.h` — into your project. It has no
build-time dependencies at all.

### Constraints worth knowing

- **One CPU per `z80_t`.** The core keeps everything in that structure and
  reads no globals, so independent instances are independent, and two threads
  may drive two CPUs at once.
- **A single `z80_t` is not thread-safe.** Drive each one from one thread.
- **No allocation after construction.** `z80_new()` allocates once; nothing on
  the tick path allocates, so it will not fragment a heap or block on one.
- **Fixed-width fields only** in `z80_pins_t` — never a pointer or a `long` —
  so 32- and 64-bit builds agree on the layout and can share a struct.

---

## 2. The disassembler

`zdasm` decodes bytes to text. It keeps no state between calls, allocates
nothing, and prints nothing.

```c
#include "zdasm.h"

zdasm_insn insn;
uint16_t pc = 0;

while (pc < size)
{
    const uint8_t used = zdasm_decode(memory, size, pc, &insn);
    if (!used) break;

    printf("%04X  %s\n", insn.address, insn.text);

    if (insn.branches)
        note_label(insn.target);      /* jumps and calls report their target */

    pc += used;
}
```

A byte that starts no known instruction comes back with `known = false`, a
length of 1, and text rendered as a `defb`, so a caller walking a buffer always
makes progress and never has to guess a length.

`zdasm_one()` is the same thing when only the text is wanted, and
`zdasm_range()` walks a span, calling you back per instruction.

The text it produces is accepted by the assembler: the project's
`round_trip_monitor48k` test disassembles a 16K ROM and assembles the listing
back to identical bytes.

---

## 3. The assembler

`zasmlib` turns source text into bytes and hands diagnostics to a callback.

```c
#include "zasm.h"

static void on_diagnostic(void *user, int line, const char *message)
{
    fprintf(stderr, "%s:%d: %s\n", (const char *)user, line, message);
}

zasm_image image = {0};

if (zasm_assemble(source_text, ZASM_FORMAT_BIN, &image, on_diagnostic, "input.asm"))
{
    memcpy(memory + image.origin, image.bytes, image.size);
    zasm_image_free(&image);
}
```

`image.origin` is the lowest address the source assembled to, so a program with
`.org 0x8000` tells you where it belongs. `ZASM_FORMAT_TAP` and
`ZASM_FORMAT_IHEX` produce those file formats into the same buffer.

Diagnostics arrive with a line number and without a trailing newline, ready for
a list box or an error window. Syntax errors from the scanner come through the
same callback as everything else.

### The one constraint

**`zasm_assemble()` is not reentrant.** The parser, the scanner and the
assembler keep their state in globals, so one assembly may be in flight at a
time in a process. Calls are independent — state is reset on entry, and
assembling twice gives the same answer twice — but they must not overlap. If
you assemble from several threads, hold a mutex around the call.

This is a property of the generated parser, not a decision. It is worth fixing
only if something needs it; nothing does today.

---

## 4. Building and shipping

### As part of a CMake project

```cmake
add_subdirectory(path/to/Z80core)
target_link_libraries(your_target PRIVATE z80core zdasm zasmlib)
```

Link only what you use. The core alone pulls in nothing else.

### As shared libraries

```sh
cmake -B build -DBUILD_SHARED_LIBS=ON
cmake --build build
```

produces `zasm` and `zdasm` as shared objects (`.dll`, `.so`, `.dylib`) with
their symbols exported on every compiler, MSVC included.

### Bitness and platforms

The libraries are plain C99 with fixed-width types and no assumptions about
pointer size, so 32- and 64-bit builds are equally supported and behave
identically. The project's own CI builds and tests on Linux and Windows.

The CPU core has no platform dependencies whatsoever and will build for
anything with a C99 compiler, freestanding targets included, provided you
supply `malloc`/`free` or replace `z80_new()` with a static instance.

`zasmlib` needs a hosted environment: a filesystem is not required, but
`malloc` and a temporary stream are, the latter only for `tap` and `ihex`.

### From another language

The libraries expose a plain C ABI, which is the easiest thing in the world to
call from anywhere:

- **C++** — the headers carry `extern "C"` guards; include and go.
- **Lua** — `src/emucore/z80core/lua_z80core.c` is a complete 5.4 binding for
  the core, and a model for binding the others.
- **Python, C#, Rust, anything with an FFI** — the structs are fixed-width
  fields with no padding surprises, and the functions take no callbacks except
  the assembler's diagnostic sink.

---

## 5. Worked example

[`tests/embed_test.c`](../tests/embed_test.c) uses `zasm.h` and `zdasm.h` and
nothing else: it assembles a program, checks the bytes, collects a deliberate
error through the callback, assembles repeatedly to prove calls are
independent, disassembles the result and reads the branch target back. It runs
as part of `ctest`, so if the libraries stop being usable from outside the
project, that test fails.

For the CPU core, [`tools/z80mon`](../tools/z80mon) is a real host: it owns
memory, drives the clock, answers the bus, and displays the pins.
