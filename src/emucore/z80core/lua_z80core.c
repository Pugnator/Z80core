/**
 * @file   lua_z80core.c
 * @brief  Lua 5.4 binding for the Z80 CPU core
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
 *
 * This file is part of Z80core, released under the terms of the GNU General
 * Public License version 2. See LICENSE.md for the full text.
 *
 * A thin wrapper over z80core.h and nothing more: it holds no state of its own
 * and implements no behaviour. Where the binding and the C API could ever
 * disagree, the C API is right.
 *
 * Loaded by an openvsm device script with require("z80core").
 */

#include "z80core.h"

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#define Z80_LUA_HANDLE "z80core.cpu"

/** Userdata: the CPU plus the pins the script drives between ticks. */
typedef struct
{
    z80_t *cpu;
    z80_pins_t pins;
} z80_lua_t;

static z80_lua_t *check_cpu(lua_State *L)
{
    z80_lua_t *self = luaL_checkudata(L, 1, Z80_LUA_HANDLE);
    if (!self->cpu)
    {
        luaL_error(L, "z80core: CPU has already been released");
    }
    return self;
}

static int l_new(lua_State *L)
{
    z80_lua_t *self = lua_newuserdatauv(L, sizeof *self, 0);
    self->cpu = NULL;
    self->pins = (z80_pins_t){0};

    luaL_setmetatable(L, Z80_LUA_HANDLE);

    self->cpu = z80_new();
    if (!self->cpu)
    {
        return luaL_error(L, "z80core: out of memory");
    }
    return 1;
}

static int l_gc(lua_State *L)
{
    z80_lua_t *self = luaL_checkudata(L, 1, Z80_LUA_HANDLE);
    z80_free(self->cpu);
    self->cpu = NULL;
    return 0;
}

/** cpu:tick(clk) -> changed mask. One clock edge, one boundary crossing. */
static int l_tick(lua_State *L)
{
    z80_lua_t *self = check_cpu(L);
    const int clk = lua_toboolean(L, 2) || (lua_isnumber(L, 2) && lua_tointeger(L, 2) != 0);

    const uint32_t changed = z80_tick(self->cpu, &self->pins, clk);

    lua_pushinteger(L, (lua_Integer)changed);
    return 1;
}

/** cpu:run(edges) -> edges. Same work, one crossing instead of one per edge. */
static int l_run(lua_State *L)
{
    z80_lua_t *self = check_cpu(L);
    const lua_Integer edges = luaL_checkinteger(L, 2);
    luaL_argcheck(L, edges >= 0, 2, "edge count must not be negative");

    const uint64_t done = z80_run(self->cpu, &self->pins, (uint64_t)edges);

    lua_pushinteger(L, (lua_Integer)done);
    return 1;
}

static int l_reset(lua_State *L)
{
    z80_lua_t *self = check_cpu(L);
    z80_reset(self->cpu);
    self->pins = (z80_pins_t){0};
    return 0;
}

static int l_edges(lua_State *L)
{
    z80_lua_t *self = check_cpu(L);
    lua_pushinteger(L, (lua_Integer)z80_edges(self->cpu));
    return 1;
}

static int l_addr(lua_State *L)
{
    z80_lua_t *self = check_cpu(L);
    lua_pushinteger(L, self->pins.A);
    return 1;
}

static int l_data(lua_State *L)
{
    z80_lua_t *self = check_cpu(L);
    lua_pushinteger(L, self->pins.D);
    return 1;
}

/** cpu:setdata(v) - what the schematic is putting on the data bus. */
static int l_setdata(lua_State *L)
{
    z80_lua_t *self = check_cpu(L);
    const lua_Integer value = luaL_checkinteger(L, 2);
    luaL_argcheck(L, value >= 0 && value <= 0xFF, 2, "data bus value must fit in a byte");
    self->pins.D = (uint8_t)value;
    return 0;
}

static int l_ctrl(lua_State *L)
{
    z80_lua_t *self = check_cpu(L);
    lua_pushinteger(L, (lua_Integer)self->pins.ctrl);
    return 1;
}

/** cpu:setctrl(mask, asserted) - drive an input pin such as WAIT or INT. */
static int l_setctrl(lua_State *L)
{
    z80_lua_t *self = check_cpu(L);
    const lua_Integer mask = luaL_checkinteger(L, 2);
    if (lua_toboolean(L, 3))
    {
        self->pins.ctrl |= (uint32_t)mask;
    }
    else
    {
        self->pins.ctrl &= ~(uint32_t)mask;
    }
    return 0;
}

static int l_version(lua_State *L)
{
    lua_pushstring(L, z80_version());
    return 1;
}

static const luaL_Reg cpu_methods[] = {
    {"tick", l_tick}, {"run", l_run},         {"reset", l_reset}, {"edges", l_edges},     {"addr", l_addr},
    {"data", l_data}, {"setdata", l_setdata}, {"ctrl", l_ctrl},   {"setctrl", l_setctrl}, {NULL, NULL}};

static const luaL_Reg module_functions[] = {{"new", l_new}, {"version", l_version}, {NULL, NULL}};

/** Control pin names, so a script never hard-codes a bit position. */
static void push_pin_constants(lua_State *L)
{
    static const struct
    {
        const char *name;
        uint32_t bit;
    } pins[] = {{"M1", Z80_M1},
                {"MREQ", Z80_MREQ},
                {"IORQ", Z80_IORQ},
                {"RD", Z80_RD},
                {"WR", Z80_WR},
                {"RFSH", Z80_RFSH},
                {"HALT", Z80_HALT},
                {"BUSAK", Z80_BUSAK},
                {"WAIT", Z80_WAIT},
                {"INT", Z80_INT},
                {"NMI", Z80_NMI},
                {"RESET", Z80_RESET},
                {"BUSRQ", Z80_BUSRQ},
                {"CHANGED_A", Z80_CHANGED_A},
                {"CHANGED_D", Z80_CHANGED_D}};

    for (size_t i = 0; i < sizeof pins / sizeof pins[0]; ++i)
    {
        lua_pushinteger(L, (lua_Integer)pins[i].bit);
        lua_setfield(L, -2, pins[i].name);
    }
}

#if defined(_WIN32)
__declspec(dllexport)
#endif
int luaopen_z80core(lua_State *L)
{
    luaL_newmetatable(L, Z80_LUA_HANDLE);
    lua_pushcfunction(L, l_gc);
    lua_setfield(L, -2, "__gc");
    lua_newtable(L);
    luaL_setfuncs(L, cpu_methods, 0);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    luaL_newlib(L, module_functions);
    push_pin_constants(L);
    return 1;
}
