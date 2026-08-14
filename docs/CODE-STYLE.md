# Code style

The C/C++ style for this repository is defined by the `.clang-format` file in
the repo root and enforced by clang-format (version 14 or newer is required
for `InsertBraces`).

## Rules

- **Braces:** Allman ("braces stand alone"), as in NASA SEL-94-003 section
  7.1.2. This applies to functions *and* control statements.
- **Bodies are always braced.** A brace-less `if (x) return;` is banned
  (deviation from SEL-94-003 7.2.1). `InsertBraces` adds missing braces
  automatically; the `AllowShort*` settings keep clang-format from collapsing
  bodies back onto one line.
- **Indentation:** 4 spaces, never tabs. Case labels are not indented.
- **Column limit:** 120.
- **Include order** is left as written (`SortIncludes: Never`) — several
  headers in this project are order-sensitive.

## Scope

Run clang-format only over hand-written sources:

- `src/asmdasm/*.c`, `include/asmdasm/*.h` — **except** `uthash.h`
  (vendored third-party code) and the generated `grammar.c` / `grammar.h` /
  `z80lex.c` (never committed; produced by bison/flex in the build tree)
- `src/emucore/*.cc`, `include/emucore/*.hpp`
- `include/resources.h`

The grammar sources `src/grammar/z80.y` and `src/grammar/z80.lex` cannot be
parsed by clang-format as a whole; keep their embedded C blocks visually
consistent with the rules above by hand.

## Usage

```sh
clang-format -i <files>
# or to verify without modifying:
clang-format --dry-run --Werror <files>
```
