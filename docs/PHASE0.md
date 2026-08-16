# Phase 0 — walking skeleton

The thinnest possible slice through the whole stack, with a tick counter where
the CPU will eventually be. It exists to answer three questions before anything
is built on top of them:

1. Will `openvsm`'s embedded interpreter load a binary Lua module at all?
2. Which compiler produces a module it will load?
3. What does one clock edge cost, through each layer?

See [CPU-CORE-SPEC.md](CPU-CORE-SPEC.md) section 12 for why this comes first.

## What exists

| Path | What it is |
| --- | --- |
| `src/emucore/z80core/include/z80core.h` | The public C API, in its final shape |
| `src/emucore/z80core/z80core.c` | The edge-stepped engine. Real engine, no instruction set: the step cursor replays one M1 fetch |
| `src/emucore/z80core/lua_z80core.c` | Lua 5.4 binding, exports `luaopen_z80core` |
| `src/emucore/z80core/bench/z80bench.c` | The core-alone benchmark |
| `src/emucore/z80core/tests/test_core.c` | Clock, mask and reset behaviour, run by `ctest` |
| `src/emucore/device/z80_device.lua` | The openvsm device script |

## Question 3, answered for the core alone

Nothing here needs Lua or Proteus:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/src/emucore/z80core/z80bench 40000000
```

On one desktop machine (x86-64, Release):

```
z80_tick() per edge              202.02 M edges/s    28.86x real time
z80_run() batched                412.37 M edges/s    58.91x real time
```

Real time for a 3.5 MHz Z80 is 7 M edges/s, so the engine has roughly **29x
headroom** before a single instruction has been implemented. Two useful
readings:

- The core will not be the bottleneck. Whatever the stack costs, it is not this.
- The gap between the two rows is the cost of *calling* — about half the time
  goes on the call itself, and that is with no language boundary in the way. It
  is a preview of why the per-edge boundary is the thing to watch.

## Questions 1 and 2 — the part that needs Proteus

### Build the Lua module

The module needs Lua 5.4 headers, and on Windows something to resolve `lua_*`
against. `openvsm` links Lua statically into its own DLL and exports its
symbols (`CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS`), so its import library is the
first thing to try:

```sh
cmake -B build32 -A Win32 \
  -DZ80CORE_LUA_INCLUDE_DIR=<openvsm>/externals/Lua/lua-5.4.6/include \
  -DZ80CORE_LUA_LIBRARY=<openvsm build>/lib/VSM_DLL.lib
cmake --build build32 --config Release
```

It must be **32-bit** to match Proteus 8, hence `-A Win32` for MSVC or an i686
toolchain for MinGW. The output is `z80core.dll` — no `lib` prefix, because
`require("z80core")` looks for a file named after the module.

If MinGW is preferred over MSVC, this is exactly where that gets decided. The
core is plain C with a C ABI, which is far more forgiving than `openvsm`'s C++
VSM interfaces, so it has a good chance of working — but "should" is why this
phase exists.

### Wire it up

1. Put `z80core.dll` next to `z80_device.lua`, or set the component's
   `ZLUA_CORE_PATH` property to the DLL's absolute path.
2. Set the component's `LUA` property to `z80_device.lua`.
3. Draw the smallest useful schematic: a clock generator on `CLK`, a pull-up or
   button on `RESET`, and `M1` / `MREQ` / `RD` brought out to probes or LEDs.
4. Run it.

### What success looks like

`log.txt` and the Proteus console should show:

```
z80: core 0.0.1-phase0 loaded
z80: 1000000 edges, ... k edges/s of simulated wall clock
```

and `M1`, `MREQ` and `RD` should pulse in the 4-T-state pattern of an opcode
fetch, one edge per clock transition.

### If it does not load

The device script prints the failure and the search path it used. In
descending order of preference, per spec section 11.2:

1. Link against the Lua symbols `openvsm.dll` exports (what the command above
   does).
2. Build Lua as a shared `lua54.dll` that both `openvsm` and this module link
   against. A small change to `openvsm`, and the conventional arrangement for
   binary Lua modules.
3. Add the core to `openvsm` as a native module, beside its existing `uart` and
   VDM ones. Cleanest technically, but then the core is no longer standalone.

Whichever it turns out to be, record it in the spec's section 13 — it is the
one open question that can still change the shape of the project.

## Exit criteria

- [x] The engine ticks, and both ways of driving it agree (`ctest -R cpu_core`)
- [x] The core-alone rate is known
- [ ] Proteus loads `z80core.dll` through the device script
- [ ] The full-stack rate is known
- [ ] The compiler question is settled

The first two are done. The last three need a Proteus install and the SDK, so
they are yours to run.
