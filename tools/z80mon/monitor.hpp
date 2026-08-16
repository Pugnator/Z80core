/**
 * @file   monitor.hpp
 * @brief  The CPU monitor: drives the core and draws its pins
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
 *
 * This file is part of Z80core, released under the terms of the GNU General
 * Public License version 2. See LICENSE.md for the full text.
 *
 * The monitor is a host like any other: it owns the memory, drives the clock
 * and answers the bus. The core knows nothing about it.
 */

#pragma once

#include "z80core.h"

#include <cstdint>
#include <string>
#include <vector>

/** One clock edge, kept so the waveform can be drawn after the fact. */
struct TraceEntry
{
    uint64_t edge;
    uint16_t address;
    uint8_t data;
    uint32_t ctrl;
    uint32_t changed;
    bool clk;
};

class Monitor
{
  public:
    Monitor();
    ~Monitor();

    /** Advance the machine for one frame and draw the interface. */
    void frame(float seconds);

  private:
    /* --- driving the core --- */

    /** One clock edge: tick, then answer whatever the core asked for. */
    void edge();
    void stepEdges(int count);
    /** Run edges until the next M1 fetch begins, so the view lands on T1. */
    void stepInstruction();
    void reset();
    void runFreely(float seconds);

    /** The host side of the bus: memory answers reads and latches writes. */
    void serviceBus();

    /* --- drawing --- */

    void drawControls();
    void drawPins();
    void drawWaveform();
    void drawMemory();

    void loadBinary(const std::string &path);

    /* --- state --- */

    z80_t *cpu_ = nullptr;
    z80_pins_t pins_ = {};
    uint32_t changed_ = 0;
    bool clk_ = false;

    std::vector<uint8_t> memory_;
    std::vector<TraceEntry> trace_;
    size_t traceHead_ = 0;
    size_t traceCount_ = 0;

    bool running_ = false;
    /** Target clock in Hz; the edge rate is twice this. */
    float clockHz_ = 1000.0f;
    bool unthrottled_ = false;
    double edgeDebt_ = 0.0;

    /* input pins the user can assert by hand */
    bool holdWait_ = false;
    bool holdInt_ = false;
    bool holdNmi_ = false;
    bool holdBusrq_ = false;

    /* what the last second actually achieved */
    double rateTimer_ = 0.0;
    uint64_t rateMark_ = 0;
    double measuredEdgeRate_ = 0.0;

    std::string status_ = "ready";
    int memoryViewBase_ = 0;
};
