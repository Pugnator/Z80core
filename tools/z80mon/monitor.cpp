/**
 * @file   monitor.cpp
 * @brief  The CPU monitor: drives the core and draws its pins
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
 *
 * This file is part of Z80core, released under the terms of the GNU General
 * Public License version 2. See LICENSE.md for the full text.
 */

#include "monitor.hpp"

#include "zdasm.h"
#include "imgui.h"

#include <windows.h>

#include <commdlg.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace
{

constexpr size_t kMemorySize = 0x10000;
constexpr size_t kTraceLength = 2048;
/** Edges per frame ceiling, so a fast clock cannot freeze the interface. */
constexpr int kMaxEdgesPerFrame = 400000;

const ImVec4 kAsserted = ImVec4(0.95f, 0.35f, 0.25f, 1.0f);
const ImVec4 kIdle = ImVec4(0.28f, 0.30f, 0.34f, 1.0f);
const ImVec4 kJustChanged = ImVec4(1.00f, 0.85f, 0.30f, 1.0f);
const ImVec4 kInputPin = ImVec4(0.30f, 0.65f, 0.95f, 1.0f);

struct PinInfo
{
    const char *name;
    uint32_t bit;
};

/**
 * Place a panel the first time it is seen, as a fraction of the window.
 *
 * Without this every panel opens at the same default spot, stacked on the
 * others. Fractions rather than pixels because the window is not always the
 * size it was asked for - display scaling sees to that - and a layout in
 * pixels leaves panels hanging off the edge. Only a starting point: the
 * panels are dockable and remember where they are put.
 */
void placePanel(float x, float y, float width, float height)
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->WorkPos;
    const ImVec2 area = viewport->WorkSize;

    ImGui::SetNextWindowPos(ImVec2(origin.x + area.x * x, origin.y + area.y * y), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(area.x * width, area.y * height), ImGuiCond_FirstUseEver);
}

/** Outputs, in the order they are worth reading. */
const PinInfo kOutputPins[] = {{"M1", Z80_M1}, {"MREQ", Z80_MREQ}, {"IORQ", Z80_IORQ}, {"RD", Z80_RD},
                               {"WR", Z80_WR}, {"RFSH", Z80_RFSH}, {"HALT", Z80_HALT}, {"BUSAK", Z80_BUSAK}};

/** Draws a signal as a lamp: lit when asserted, ringed when it just moved. */
void signalLamp(const char *label, bool asserted, bool changed, const ImVec4 &onColour)
{
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float size = ImGui::GetTextLineHeight();
    const float radius = size * 0.42f;
    const ImVec2 centre(pos.x + radius, pos.y + size * 0.5f);

    draw->AddCircleFilled(centre, radius, ImGui::GetColorU32(asserted ? onColour : kIdle));
    if (changed)
    {
        draw->AddCircle(centre, radius + 2.0f, ImGui::GetColorU32(kJustChanged), 0, 2.0f);
    }

    ImGui::Dummy(ImVec2(radius * 2.0f + 6.0f, size));
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
}

/** A row of bits, most significant first, with the value beside it. */
void busBits(const char *label, uint32_t value, int width, bool changed)
{
    ImGui::TextUnformatted(label);
    ImGui::SameLine(70.0f);

    ImDrawList *draw = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    const float box = ImGui::GetTextLineHeight() * 0.72f;
    const float gap = 3.0f;

    for (int bit = width - 1; bit >= 0; --bit)
    {
        const bool set = (value >> bit) & 1u;
        const float x = pos.x + (float)(width - 1 - bit) * (box + gap);
        const ImVec2 a(x, pos.y + 2.0f);
        const ImVec2 b(x + box, pos.y + 2.0f + box);
        draw->AddRectFilled(a, b, ImGui::GetColorU32(set ? kAsserted : kIdle), 2.0f);
        if (bit % 4 == 0 && bit != 0)
        {
            draw->AddLine(ImVec2(b.x + gap * 0.5f, a.y), ImVec2(b.x + gap * 0.5f, b.y),
                          ImGui::GetColorU32(ImGuiCol_Separator));
        }
    }

    ImGui::Dummy(ImVec2((float)width * (box + gap), box + 4.0f));
    ImGui::SameLine();
    if (changed)
    {
        ImGui::TextColored(kJustChanged, width > 8 ? "%04X" : "%02X", value);
    }
    else
    {
        ImGui::Text(width > 8 ? "%04X" : "%02X", value);
    }
}

std::wstring widen(const std::string &text)
{
    if (text.empty())
    {
        return std::wstring();
    }
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0);
    std::wstring wide((size_t)needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), wide.data(), needed);
    return wide;
}

std::string narrow(const std::wstring &text)
{
    if (text.empty())
    {
        return std::string();
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string narrowed((size_t)needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), narrowed.data(), needed, nullptr, nullptr);
    return narrowed;
}

/** The common file dialog. Returns an empty string when the user cancels. */
std::string chooseFile(const wchar_t *filter, const wchar_t *title, bool saving)
{
    wchar_t path[MAX_PATH] = L"";

    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof dialog;
    dialog.hwndOwner = GetActiveWindow();
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrTitle = title;
    dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | (saving ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);

    const BOOL chosen = saving ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
    return chosen ? narrow(path) : std::string();
}

/** The directory holding this executable, where zasm sits beside it. */
std::string executableDirectory()
{
    wchar_t path[MAX_PATH] = L"";
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring full(path);
    const size_t slash = full.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::string() : narrow(full.substr(0, slash));
}

/**
 * Run a command and collect everything it prints.
 *
 * CreateProcess rather than _popen: this is a windowed program, and _popen
 * flashes a console every time the user presses Assemble.
 */
bool runCapturing(const std::wstring &commandLine, std::string &output, DWORD &exitCode)
{
    SECURITY_ATTRIBUTES inheritable = {};
    inheritable.nLength = sizeof inheritable;
    inheritable.bInheritHandle = TRUE;

    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &inheritable, 0))
    {
        output = "could not create a pipe";
        return false;
    }
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup = {};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = writeEnd;
    startup.hStdError = writeEnd;

    PROCESS_INFORMATION process = {};
    std::wstring mutableCommand = commandLine;

    const BOOL started = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                        nullptr, nullptr, &startup, &process);
    CloseHandle(writeEnd);
    if (!started)
    {
        CloseHandle(readEnd);
        output = "could not start zasm";
        return false;
    }

    char buffer[512];
    DWORD read = 0;
    while (ReadFile(readEnd, buffer, sizeof buffer, &read, nullptr) && read > 0)
    {
        output.append(buffer, read);
    }
    CloseHandle(readEnd);

    WaitForSingleObject(process.hProcess, INFINITE);
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    return true;
}

const char *kStarterProgram = "; z80mon scratch program\n"
                              "; Assemble replaces memory without disturbing the CPU.\n"
                              "\n"
                              "    .org 0\n"
                              "start:\n"
                              "    LD A, 0x55\n"
                              "    LD HL, 0x8000\n"
                              "    LD [HL], A\n"
                              "    INC HL\n"
                              "    JR start\n"
                              "    .end\n";

} // namespace

Monitor::Monitor() : memory_(kMemorySize, 0x00), trace_(kTraceLength), source_(64 * 1024, '\0')
{
    cpu_ = z80_new();

    /* Something to watch by default: NOPs everywhere, so the core fetches
       forever and every M1 cycle is visible on the waveform. */
    std::fill(memory_.begin(), memory_.end(), 0x00);

    std::snprintf(source_.data(), source_.size(), "%s", kStarterProgram);
}

Monitor::~Monitor()
{
    z80_free(cpu_);
}

void Monitor::serviceBus()
{
    /* The host owns memory. The core never sees any of this; it only finds a
       value on the data bus when it samples it. */
    if ((pins_.ctrl & Z80_MREQ) && (pins_.ctrl & Z80_RD))
    {
        pins_.D = memory_[pins_.A];
    }
    else if ((pins_.ctrl & Z80_MREQ) && (pins_.ctrl & Z80_WR))
    {
        memory_[pins_.A] = pins_.D;
    }
}

void Monitor::edge()
{
    clk_ = !clk_;

    /* input pins the user is holding */
    const uint32_t inputs = (holdWait_ ? (uint32_t)Z80_WAIT : 0u) | (holdInt_ ? (uint32_t)Z80_INT : 0u) |
                            (holdNmi_ ? (uint32_t)Z80_NMI : 0u) | (holdBusrq_ ? (uint32_t)Z80_BUSRQ : 0u);
    pins_.ctrl = (pins_.ctrl & ~(uint32_t)(Z80_WAIT | Z80_INT | Z80_NMI | Z80_BUSRQ)) | inputs;

    changed_ = z80_tick(cpu_, &pins_, clk_ ? 1 : 0);

    serviceBus();

    TraceEntry &entry = trace_[traceHead_];
    entry.edge = z80_edges(cpu_);
    entry.address = pins_.A;
    entry.data = pins_.D;
    entry.ctrl = pins_.ctrl;
    entry.changed = changed_;
    entry.clk = clk_;

    traceHead_ = (traceHead_ + 1) % trace_.size();
    if (traceCount_ < trace_.size())
    {
        ++traceCount_;
    }
}

void Monitor::stepEdges(int count)
{
    for (int i = 0; i < count; ++i)
    {
        edge();
    }
}

void Monitor::stepInstruction()
{
    /* Run until M1 rises again, which is the start of the next fetch. Bounded
       so a core that never asserts M1 cannot hang the interface. */
    const int limit = 4096;
    for (int i = 0; i < limit; ++i)
    {
        const bool wasM1 = (pins_.ctrl & Z80_M1) != 0;
        edge();
        const bool isM1 = (pins_.ctrl & Z80_M1) != 0;
        if (!wasM1 && isM1)
        {
            return;
        }
    }
    status_ = "no M1 within 4096 edges";
}

void Monitor::reset()
{
    z80_reset(cpu_);
    pins_ = z80_pins_t{};
    changed_ = 0;
    clk_ = false;
    traceHead_ = 0;
    traceCount_ = 0;
    edgeDebt_ = 0.0;
    status_ = "reset";
}

void Monitor::runFreely(float seconds)
{
    if (!running_)
    {
        return;
    }

    if (unthrottled_)
    {
        stepEdges(kMaxEdgesPerFrame);
        return;
    }

    /* Two edges per clock period. Carry the fraction so slow clocks stay
       accurate instead of rounding to zero every frame. */
    edgeDebt_ += (double)seconds * (double)clockHz_ * 2.0;
    int due = (int)edgeDebt_;
    if (due > kMaxEdgesPerFrame)
    {
        due = kMaxEdgesPerFrame;
        edgeDebt_ = 0.0;
        status_ = "clock faster than the display can follow";
    }
    else
    {
        edgeDebt_ -= (double)due;
    }
    stepEdges(due);
}

void Monitor::loadBinary(const std::string &path)
{
    FILE *file = fopen(path.c_str(), "rb");
    if (!file)
    {
        status_ = "cannot open " + path;
        return;
    }
    std::fill(memory_.begin(), memory_.end(), 0x00);
    const size_t read = fread(memory_.data(), 1, memory_.size(), file);
    fclose(file);

    /* Memory changes, the CPU does not. Reset is the button for starting
       over, and it stays the only thing that does. */
    char message[320];
    snprintf(message, sizeof message, "loaded %zu bytes, CPU untouched at edge %llu", read,
             (unsigned long long)z80_edges(cpu_));
    status_ = message;
}

void Monitor::loadSource(const std::string &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        status_ = "cannot open " + path;
        return;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    const std::string text = contents.str();

    std::fill(source_.begin(), source_.end(), '\0');
    std::memcpy(source_.data(), text.c_str(), std::min(text.size(), source_.size() - 1));
    sourcePath_ = path;
    status_ = "opened " + path;
}

void Monitor::saveSource(const std::string &path)
{
    std::ofstream file(path, std::ios::binary);
    if (!file)
    {
        status_ = "cannot write " + path;
        return;
    }
    file << source_.data();
    sourcePath_ = path;
    status_ = "saved " + path;
}

void Monitor::assemble()
{
    const std::string directory = executableDirectory();
    const std::string assembler = directory + "\\zasm.exe";

    wchar_t temporary[MAX_PATH] = L"";
    GetTempPathW(MAX_PATH, temporary);
    const std::string base = narrow(temporary) + "z80mon_scratch";
    const std::string sourceFile = base + ".asm";
    const std::string binaryFile = base + ".bin";

    {
        std::ofstream file(sourceFile, std::ios::binary);
        if (!file)
        {
            assemblerOutput_ = "cannot write " + sourceFile;
            assembleFailed_ = true;
            return;
        }
        file << source_.data();
    }
    DeleteFileW(widen(binaryFile).c_str());

    const std::wstring command =
        L"\"" + widen(assembler) + L"\" -s \"" + widen(sourceFile) + L"\" -o \"" + widen(binaryFile) + L"\"";

    DWORD exitCode = 1;
    assemblerOutput_.clear();
    if (!runCapturing(command, assemblerOutput_, exitCode))
    {
        assemblerOutput_ += "\nlooked for zasm at " + assembler;
        assembleFailed_ = true;
        return;
    }

    if (exitCode != 0)
    {
        assembleFailed_ = true;
        if (assemblerOutput_.empty())
        {
            assemblerOutput_ = "zasm failed with no output";
        }
        status_ = "assembly failed";
        return;
    }

    assembleFailed_ = false;
    loadBinary(binaryFile);
    if (assemblerOutput_.empty())
    {
        assemblerOutput_ = "assembled cleanly";
    }
}

void Monitor::drawControls()
{
    placePanel(0.00f, 0.00f, 0.29f, 0.60f);
    ImGui::Begin("Clock");

    if (ImGui::Button(running_ ? "Stop" : "Run", ImVec2(70, 0)))
    {
        running_ = !running_;
        edgeDebt_ = 0.0;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset", ImVec2(70, 0)))
    {
        reset();
    }

    ImGui::SeparatorText("Step by hand");
    ImGui::BeginDisabled(running_);
    if (ImGui::Button("Edge", ImVec2(70, 0)))
    {
        stepEdges(1);
    }
    ImGui::SameLine();
    ImGui::SetItemTooltip("half a T-state: the smallest thing the core does");
    if (ImGui::Button("T-state", ImVec2(70, 0)))
    {
        stepEdges(2);
    }
    ImGui::SameLine();
    if (ImGui::Button("M1 cycle", ImVec2(70, 0)))
    {
        stepEdges(8);
    }
    ImGui::SameLine();
    if (ImGui::Button("Instruction", ImVec2(90, 0)))
    {
        stepInstruction();
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("Free running");
    ImGui::Checkbox("As fast as possible", &unthrottled_);
    ImGui::BeginDisabled(unthrottled_);
    ImGui::SetNextItemWidth(220.0f);
    ImGui::SliderFloat("Clock", &clockHz_, 1.0f, 4000000.0f, "%.0f Hz", ImGuiSliderFlags_Logarithmic);
    ImGui::EndDisabled();

    ImGui::SeparatorText("Hold input pins");
    ImGui::Checkbox("WAIT", &holdWait_);
    ImGui::SameLine();
    ImGui::Checkbox("INT", &holdInt_);
    ImGui::SameLine();
    ImGui::Checkbox("NMI", &holdNmi_);
    ImGui::SameLine();
    ImGui::Checkbox("BUSRQ", &holdBusrq_);

    ImGui::SeparatorText("Counters");
    const uint64_t edges = z80_edges(cpu_);
    ImGui::Text("edges     %llu", (unsigned long long)edges);
    ImGui::Text("T-states  %llu", (unsigned long long)(edges / 2));
    ImGui::Text("measured  %.2f kHz clock, %.1f k edges/s", measuredEdgeRate_ / 2000.0, measuredEdgeRate_ / 1000.0);

    ImGui::Separator();
    ImGui::TextDisabled("%s", status_.c_str());

    ImGui::End();
}

void Monitor::drawPins()
{
    placePanel(0.00f, 0.60f, 0.29f, 0.40f);
    ImGui::Begin("Pins");

    busBits("A", pins_.A, 16, (changed_ & Z80_CHANGED_A) != 0);
    busBits("D", pins_.D, 8, (changed_ & Z80_CHANGED_D) != 0);

    ImGui::SeparatorText("Driven by the CPU");
    for (size_t i = 0; i < sizeof kOutputPins / sizeof kOutputPins[0]; ++i)
    {
        const PinInfo &pin = kOutputPins[i];
        signalLamp(pin.name, (pins_.ctrl & pin.bit) != 0, (changed_ & pin.bit) != 0, kAsserted);
        if (i % 2 == 0)
        {
            ImGui::SameLine(110.0f);
        }
    }

    ImGui::SeparatorText("Driven by the host");
    signalLamp("CLK", clk_, true, kInputPin);
    ImGui::SameLine(110.0f);
    signalLamp("WAIT", holdWait_, false, kInputPin);
    signalLamp("INT", holdInt_, false, kInputPin);
    ImGui::SameLine(110.0f);
    signalLamp("NMI", holdNmi_, false, kInputPin);

    ImGui::Separator();
    ImGui::TextDisabled("a ring marks a pin that moved on the last edge");

    ImGui::End();
}

void Monitor::drawWaveform()
{
    placePanel(0.29f, 0.00f, 0.71f, 0.30f);
    ImGui::Begin("Waveform");

    static const PinInfo traces[] = {{"M1", Z80_M1}, {"MREQ", Z80_MREQ}, {"RD", Z80_RD},
                                     {"WR", Z80_WR}, {"IORQ", Z80_IORQ}, {"RFSH", Z80_RFSH}};
    const int traceRows = (int)(sizeof traces / sizeof traces[0]) + 1; /* +1 for CLK */

    const int captured = (int)traceCount_;
    ImGui::TextDisabled("most recent %d edges of %d captured, oldest on the left", std::min(captured, 256), captured);

    const float rowHeight = 22.0f;
    const float labelWidth = 46.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x - labelWidth - 8.0f;
    ImDrawList *draw = ImGui::GetWindowDrawList();

    const int visible = std::min<int>((int)traceCount_, 256);
    const float stepX = visible > 1 ? width / (float)visible : width;

    for (int row = 0; row < traceRows; ++row)
    {
        const char *name = row == 0 ? "CLK" : traces[row - 1].name;
        const uint32_t bit = row == 0 ? 0u : traces[row - 1].bit;

        const float top = origin.y + (float)row * rowHeight;
        const float high = top + 3.0f;
        const float low = top + rowHeight - 7.0f;

        draw->AddText(ImVec2(origin.x, top + 2.0f), ImGui::GetColorU32(ImGuiCol_Text), name);

        float previousY = low;
        for (int i = 0; i < visible; ++i)
        {
            /* walk forward from the oldest visible entry */
            const size_t index = (traceHead_ + trace_.size() - (size_t)visible + (size_t)i) % trace_.size();
            const TraceEntry &entry = trace_[index];
            const bool level = row == 0 ? entry.clk : (entry.ctrl & bit) != 0;

            const float x0 = origin.x + labelWidth + (float)i * stepX;
            const float x1 = x0 + stepX;
            const float y = level ? high : low;

            if (i > 0 && y != previousY)
            {
                draw->AddLine(ImVec2(x0, previousY), ImVec2(x0, y), ImGui::GetColorU32(kAsserted), 1.5f);
            }
            draw->AddLine(ImVec2(x0, y), ImVec2(x1, y), ImGui::GetColorU32(kAsserted), 1.5f);
            previousY = y;
        }
    }

    ImGui::Dummy(ImVec2(width, (float)traceRows * rowHeight + 6.0f));
    ImGui::TextDisabled("one step per clock edge, so a T-state is two steps wide");

    ImGui::End();
}

void Monitor::drawMemory()
{
    placePanel(0.29f, 0.30f, 0.71f, 0.28f);
    ImGui::Begin("Memory");

    if (ImGui::Button("Load .bin..."))
    {
        const std::string path = chooseFile(L"Binary images\0*.bin\0All files\0*.*\0\0", L"Load a binary image", false);
        if (!path.empty())
        {
            loadBinary(path);
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("loaded at 0000; the CPU keeps running");

    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputInt("base", &memoryViewBase_, 16, 256, ImGuiInputTextFlags_CharsHexadecimal);
    memoryViewBase_ = std::clamp(memoryViewBase_, 0, (int)kMemorySize - 256);

    ImGui::BeginChild("dump", ImVec2(0, 0), ImGuiChildFlags_Borders);
    for (int row = 0; row < 16; ++row)
    {
        const int base = memoryViewBase_ + row * 16;
        ImGui::Text("%04X ", base);
        for (int column = 0; column < 16; ++column)
        {
            ImGui::SameLine();
            const int address = base + column;
            const bool onBus = address == (int)pins_.A;
            if (onBus)
            {
                ImGui::TextColored(kJustChanged, "%02X", memory_[address]);
            }
            else
            {
                ImGui::Text("%02X", memory_[address]);
            }
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

void Monitor::drawDisassembly()
{
    placePanel(0.29f, 0.58f, 0.33f, 0.42f);
    ImGui::Begin("Disassembly");

    ImGui::Checkbox("Follow the CPU", &disasmFollowsCpu_);
    ImGui::SetItemTooltip("keep the view on whatever address the CPU is putting on the bus");

    if (disasmFollowsCpu_)
    {
        /* Bias backwards a little so the current address is not glued to the
           top edge, and clamp so the window never runs off the end. */
        const int centred = (int)pins_.A - 8;
        disasmBase_ = std::clamp(centred, 0, (int)kMemorySize - 1);
    }
    else
    {
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputInt("from", &disasmBase_, 1, 16, ImGuiInputTextFlags_CharsHexadecimal);
        disasmBase_ = std::clamp(disasmBase_, 0, (int)kMemorySize - 1);
    }

    ImGui::Separator();
    ImGui::BeginChild("listing", ImVec2(0, 0), ImGuiChildFlags_Borders);

    uint16_t address = (uint16_t)disasmBase_;
    for (int line = 0; line < disasmLines_; ++line)
    {
        char text[128];
        const uint8_t used = zdasm_one(memory_.data(), memory_.size(), address, text, sizeof text);
        const bool here = address == pins_.A;

        /* the bytes this instruction occupies, so the listing reads like one */
        char bytes[16] = "";
        for (size_t i = 0; i < used && i < 4; ++i)
        {
            char pair[4];
            snprintf(pair, sizeof pair, "%02X ", memory_[(uint16_t)(address + i)]);
            strncat(bytes, pair, sizeof bytes - strlen(bytes) - 1);
        }

        if (here)
        {
            ImGui::TextColored(kJustChanged, "%04X  %-12s %s", address, bytes, text);
        }
        else
        {
            ImGui::Text("%04X  %-12s %s", address, bytes, text);
        }

        if (used == 0)
        {
            break;
        }
        const uint32_t next = (uint32_t)address + (uint32_t)used;
        if (next > 0xFFFF)
        {
            break;
        }
        address = (uint16_t)next;
    }

    ImGui::EndChild();
    ImGui::End();
}

void Monitor::drawEditor()
{
    placePanel(0.62f, 0.58f, 0.38f, 0.42f);
    ImGui::Begin("Assembler");

    if (ImGui::Button("Assemble"))
    {
        assemble();
    }
    ImGui::SetItemTooltip("build with zasm and replace memory, leaving the CPU where it is");
    ImGui::SameLine();
    if (ImGui::Button("Open..."))
    {
        const std::string path =
            chooseFile(L"Assembly source\0*.asm;*.z80;*.s\0All files\0*.*\0\0", L"Open source", false);
        if (!path.empty())
        {
            loadSource(path);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save as..."))
    {
        const std::string path = chooseFile(L"Assembly source\0*.asm\0All files\0*.*\0\0", L"Save source", true);
        if (!path.empty())
        {
            saveSource(path);
        }
    }

    if (!sourcePath_.empty())
    {
        ImGui::TextDisabled("%s", sourcePath_.c_str());
    }

    const float outputHeight = 96.0f;
    const float editorHeight = ImGui::GetContentRegionAvail().y - outputHeight - ImGui::GetTextLineHeight() * 2.0f;
    ImGui::InputTextMultiline("##source", source_.data(), source_.size(), ImVec2(-1.0f, std::max(editorHeight, 120.0f)),
                              ImGuiInputTextFlags_AllowTabInput);

    ImGui::SeparatorText(assembleFailed_ ? "zasm: failed" : "zasm");
    ImGui::BeginChild("output", ImVec2(0, outputHeight), ImGuiChildFlags_Borders);
    if (assembleFailed_)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, kAsserted);
    }
    ImGui::TextUnformatted(assemblerOutput_.empty() ? "press Assemble" : assemblerOutput_.c_str());
    if (assembleFailed_)
    {
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();

    ImGui::End();
}

void Monitor::frame(float seconds)
{
    runFreely(seconds);

    /* measure what the clock actually achieved, once a second */
    rateTimer_ += (double)seconds;
    if (rateTimer_ >= 1.0)
    {
        const uint64_t now = z80_edges(cpu_);
        measuredEdgeRate_ = (double)(now - rateMark_) / rateTimer_;
        rateMark_ = now;
        rateTimer_ = 0.0;
    }

    drawControls();
    drawPins();
    drawWaveform();
    drawDisassembly();
    drawEditor();
    drawMemory();
}
