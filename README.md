![Zilog logo](Zilog.png)

[![CI](https://github.com/Pugnator/Z80core/actions/workflows/ci.yml/badge.svg)](https://github.com/Pugnator/Z80core/actions/workflows/ci.yml)
[![License: GPL v2](https://img.shields.io/badge/license-GPL--2.0-blue.svg)](LICENSE.md)

#### Set of tools for Z80 DIY projects

`zasm` is a Z80 assembler and disassembler supporting the documented and the
undocumented instruction set.

###### What is in here

| | |
| --- | --- |
| `zasm` | Z80 assembler and disassembler, all documented and undocumented opcodes |
| `z80core` | the CPU core: a C library, edge-precise, no memory and no devices ([spec](docs/CPU-CORE-SPEC.md)) |
| `z80mon` | an ImGui window onto the core: pins, clock, waveform ([readme](tools/z80mon/README.md)) |

###### Building

Needs **GCC** (MSYS2 UCRT64 on Windows), CMake 3.21+, Ninja, bison and flex.
Python 3 is optional and enables most of the test suite.

```sh
cmake --preset default          # zasm and the CPU core
cmake --build --preset default
ctest --preset default
```

Everything runnable lands in `build/default/bin` — `zasm`, the core's tests and
benchmark, and the conformance harnesses. Static and import libraries go to
`build/default/lib`.

To include the ImGui monitor — this one fetches Dear ImGui, so it needs the
network once:

```sh
cmake --preset tools
cmake --build --preset tools
./build/tools/bin/z80mon
```

`--preset debug` builds the same targets with symbols.

### The core in both word sizes

Proteus and `openvsm` are 32-bit; everything else here is 64. The core is meant
to be usable from both, and on Windows the two come from separate toolchains
rather than a compiler flag — the 64-bit MinGW gcc has no `-m32`. So it is two
configures, each run from the matching MSYS2 shell:

```sh
# from an MSYS2 UCRT64 (or MINGW64) shell
cmake --preset core64 && cmake --build --preset core64

# from an MSYS2 MINGW32 shell
cmake --preset core32 && cmake --build --preset core32
```

Each writes `z80core.dll` to `build/core64/bin` and `build/core32/bin`. The name
is deliberately the same in both: `require("z80core")` and `LoadLibrary` want
that file name, so the word size is carried by the directory rather than by
mangling the name.

Configure prints which one it is — `z80core: building 32-bit` — because nothing
else about the command line says so.

**Use the presets, or pass `-G Ninja` yourself.** A bare `cmake -B build`
picks whatever generator CMake defaults to, which on a machine with Visual
Studio installed is MSVC — and `zasm` does not build with MSVC, because it
uses `unistd.h`, `strings.h` and `getopt.h`. The failure is a confusing wall
of missing-header errors rather than anything that names the real problem.
The core itself is clean C99 and does compile with MSVC.

###### Using

```sh
zasm -s program.asm -o program.bin           # assemble (default format: bin)
zasm -s program.asm -o program.tap -x tap    # ZX Spectrum tape image
zasm -s program.asm -o program.hex -x ihex   # Intel HEX
zasm -d -s program.bin                       # disassemble to stdout
```

`zasm -h` lists every option. A non-zero exit status means the assembly failed.

###### Syntax notes

- Numbers: `255`, `0xFF`, `$FF`, `0FFh` (the `h` suffix needs a leading digit,
  so `abch` is a label and `0abch` is a number), `o377`, `0b1011`, `%1011`.
- `%` is a binary literal when binary digits follow it immediately and the
  modulo operator otherwise: `defb %1011` is a literal, `defb 5 % 3` is
  modulo, and `defb 5 %11` is *not* modulo. Prefer `0b1011` and spaces
  around `%`.
- Operators, lowest precedence first: `|`, `^`, `&`, `<<` `>>`, `+` `-`,
  `*` `/` `%`, then unary `-` and `~`. Parentheses are reserved for
  addressing modes and cannot group expressions.
- Comments run to end of line and start with `;`, `//` or `#`.
- Labels end with `:` (anywhere on the line) or start at column 0. Register,
  flag and instruction names are reserved and cannot be labels.

###### Tests

| Test | What it checks |
| --- | --- |
| `rom_monitor48k` | `tests/monitor48k/monitor48k.asm` assembles to a byte-exact copy of the real 16K ZX Spectrum ROM |
| `round_trip_monitor48k` | that ROM disassembles and reassembles back to the same 16384 bytes |
| `opcode_suite` | every entry in `tests/asm_test.asm` assembles to the expected bytes |
| `opcode_table_invariants` | `z80tab.c` stays sorted, has one primary encoding per mnemonic and one mnemonic per opcode, and keeps `data_size` consistent with each format string |
| `opcode_coverage` | every instruction in the table is reachable from the grammar and assembles to the bytes the table specifies |
| `selftest_fallingblocks` | a second real program still assembles |
| `ihex_output` | Intel HEX output parses, every checksum is right, and decodes back to the reference image |
| `cpu_core` | the core's clock, changed-pin mask and reset behave (`src/emucore/z80core`) |
| `cpu_core_benchmark` | the edge benchmark still runs; the rate it prints is for humans, nothing fails on being slow |

###### Using it in your own program

The CPU core, the assembler and the disassembler are libraries as well as
tools, static or shared, with plain C headers. See
[docs/EMBEDDING.md](docs/EMBEDDING.md).

```cmake
add_subdirectory(path/to/Z80core)
target_link_libraries(your_target PRIVATE z80core zdasm zasmlib)
```

###### Continuous integration

Every pull request against `master` is built and tested on Linux and Windows,
and checked against `.clang-format`. The build treats warnings as errors
(`-Wall -Wextra -Werror`) for all hand-written code; the generated parser and
scanner are exempt, since their warnings belong to bison and flex.

###### Code style

See [docs/CODE-STYLE.md](docs/CODE-STYLE.md); the rules live in `.clang-format`.

###### License

Z80core is copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core
contributors, released under the **GNU General Public License, version 2**.
See [LICENSE.md](LICENSE.md) for the full text and [AUTHORS.md](AUTHORS.md)
for contributors and third-party code.

###### TODO

- [ ] Macro assembler/disassembler
- [ ] Cycle-accurate emulator (in progress: [spec](docs/CPU-CORE-SPEC.md), [phase 0](docs/PHASE0.md))
- [ ] OpenVSM support (the core reaches Proteus through an openvsm device script)
- [ ] A language above assembly — macro layer, then Forth or a C subset
      ([the argument](docs/SDK-LANGUAGE.md))
