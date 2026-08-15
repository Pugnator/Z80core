![Zilog logo](Zilog.png)

[![CI](https://github.com/Pugnator/Z80core/actions/workflows/ci.yml/badge.svg)](https://github.com/Pugnator/Z80core/actions/workflows/ci.yml)
[![License: GPL v2](https://img.shields.io/badge/license-GPL--2.0-blue.svg)](LICENSE.md)

#### Set of tools for Z80 DIY projects

`zasm` is a Z80 assembler and disassembler supporting the documented and the
undocumented instruction set.

###### Building

Requires a C99 compiler, CMake 3.13+, bison and flex. Python 3 is optional and
enables the test suite.

```sh
cmake -B build
cmake --build build
ctest --test-dir build
```

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
- [ ] Cycle-accurate emulator
- [ ] OpenVSM support
- [ ] Lua to Forth translator
