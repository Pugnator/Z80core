/**
 * @file   exec.cc
 * @brief  Emulator core: instruction execution
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
 *
 * This file is part of Z80core, released under the terms of the GNU General
 * Public License version 2. See LICENSE.md for the full text.
 */

#include <stdio.h>
#include <cpu.hpp>

void emu_start(void)
{
    puts("Starting Emu");
    std::shared_ptr<vcpu> test();
}

int main(int argc, char **argv)
{
    emu_start();
}
