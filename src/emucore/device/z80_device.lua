-- z80_device.lua - openvsm device script for the Z80 core
--
-- SPDX-License-Identifier: GPL-2.0-only
-- Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
--
-- This file is part of Z80core, released under the terms of the GNU General
-- Public License version 2. See LICENSE.md for the full text.
--
-- PHASE 0. This script exists to answer three questions and nothing else:
--   1. will openvsm's interpreter load a binary module at all,
--   2. does the core follow the schematic's clock net,
--   3. how many edges a second survive the whole stack.
-- It drives no address or data bus yet; that arrives with phase 2.

SAFE_MODE = true
LOGIC_TYPE = TTL

device_pins = {
    {name = "CLK", on_time = 10000, off_time = 10000},
    {name = "RESET", on_time = 10000, off_time = 10000},
    {name = "M1", on_time = 10000, off_time = 10000},
    {name = "MREQ", on_time = 10000, off_time = 10000},
    {name = "RD", on_time = 10000, off_time = 10000}
}

-- How often to report throughput, in edges.
local REPORT_EVERY = 1000000

local z80
local cpu
local edges = 0
local reported_at = 0
local started_at

--- Load z80core.dll.
-- require() searches package.cpath, which will not contain the directory this
-- script came from, so that directory is added first. An explicit
-- ZLUA_CORE_PATH property overrides the search entirely.
local function load_core()
    local direct = ZLUA_CORE_PATH
    if type(direct) == "string" and #direct > 0 then
        local loader, err = package.loadlib(direct, "luaopen_z80core")
        if not loader then
            return nil, string.format("package.loadlib(%q) failed: %s", direct, tostring(err))
        end
        return loader()
    end

    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then
        source = source:sub(2)
    end
    local directory = source:match("^(.*[\\/])")
    if directory then
        package.cpath = directory .. "?.dll;" .. package.cpath
    end

    local ok, result = pcall(require, "z80core")
    if not ok then
        return nil, tostring(result)
    end
    return result
end

function device_init()
    local core, err = load_core()
    if not core then
        print("z80: cannot load z80core - " .. err)
        print("z80: package.cpath = " .. package.cpath)
        return
    end

    z80 = core
    cpu = z80.new()
    edges = 0
    reported_at = 0
    started_at = systime()

    print(string.format("z80: core %s loaded", z80.version()))

    -- One tick per clock edge. This callback is the whole interface: openvsm
    -- reports the new level, the core advances, and whatever the core drove
    -- to a new value gets published.
    CLK:onchange(function(_, _, state)
        local changed = cpu:tick(state == SHI and 1 or 0)
        edges = edges + 1

        if changed ~= 0 then
            publish(changed)
        end

        if edges - reported_at >= REPORT_EVERY then
            report_rate()
            reported_at = edges
        end
    end)

    RESET:onchange(function(_, _, state)
        cpu:setctrl(z80.RESET, state == SLO)
    end)
end

--- Publish only the pins that moved, each with its own propagation delay.
-- The core reports what changed; when it changed in analog time is this
-- script's business, not the core's.
function publish(changed)
    local ctrl = cpu:ctrl()

    if changed & z80.M1 ~= 0 then
        M1:set(ctrl & z80.M1 ~= 0 and SLO or SHI)
    end
    if changed & z80.MREQ ~= 0 then
        MREQ:set(ctrl & z80.MREQ ~= 0 and SLO or SHI)
    end
    if changed & z80.RD ~= 0 then
        RD:set(ctrl & z80.RD ~= 0 and SLO or SHI)
    end
    -- Address and data buses arrive in phase 2; changed carries CHANGED_A and
    -- CHANGED_D for them already.
end

function report_rate()
    local elapsed = (systime() - started_at) / 1e9
    if elapsed > 0 then
        print(string.format("z80: %d edges, %.1f k edges/s of simulated wall clock", edges, edges / elapsed / 1000))
    end
end

function device_simulate()
end
