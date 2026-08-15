/**
 * @file   cpu.cc
 * @brief  Emulator core: CPU state
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
 *
 * This file is part of Z80core, released under the terms of the GNU General
 * Public License version 2. See LICENSE.md for the full text.
 */

#include <cpu.hpp>
#include <stdio.h>

void vcpu::reset()
{
    std::fill(cached_ram->begin(), cached_ram->end(), 0);
    state = FETCH;
    flag = NONE;
    pins = {};
    regs = {};
    regs.PC = 0xFFFF;
    regs.SP = 0xFFFF;
    clocks_passed = 0;
}

void vcpu::clock()
{
}

void vcpu::fetch()
{
}
