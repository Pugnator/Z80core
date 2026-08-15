/**
 * @file   compat.c
 * @brief  Compatibility workarounds for platforms missing vasprintf
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
 *
 * This file is part of Z80core, released under the terms of the GNU General
 * Public License version 2. See LICENSE.md for the full text.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

int vasprintf(char **sptr, const char *fmt, va_list argv)
{
    int wanted = vsnprintf(*sptr = NULL, 0, fmt, argv);
    if (wanted < 0)
    {
        return -1;
    }
    ++wanted;
    *sptr = malloc(wanted);
    if (*sptr == 0)
    {
        return -1;
    }
    return vsnprintf(*sptr, wanted, fmt, argv);
}
