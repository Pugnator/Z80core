![Zilog logo](Zilog.png)

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

###### Tests

| Test | What it checks |
| --- | --- |
| `rom_monitor48k` | `tests/monitor48k/monitor48k.asm` assembles to a byte-exact copy of the real 16K ZX Spectrum ROM |
| `round_trip_monitor48k` | that ROM disassembles and reassembles back to the same 16384 bytes |
| `opcode_suite` | every entry in `tests/asm_test.asm` assembles to the expected bytes |
| `opcode_table_invariants` | `z80tab.c` stays sorted, has one primary encoding per mnemonic, and keeps `data_size` consistent with each format string |
| `selftest_fallingblocks` | a second real program still assembles |

###### Code style

See [docs/CODE-STYLE.md](docs/CODE-STYLE.md); the rules live in `.clang-format`.

###### TODO

- [ ] Macro assembler/disassembler
- [ ] Cycle-accurate emulator
- [ ] OpenVSM support
- [ ] Lua to Forth translator
