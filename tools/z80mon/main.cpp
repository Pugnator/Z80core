/**
 * @file   main.cpp
 * @brief  Win32 window and OpenGL context for the CPU monitor
 *
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2016-2026 Lavrentiy Ivanov and the Z80core contributors
 *
 * This file is part of Z80core, released under the terms of the GNU General
 * Public License version 2. See LICENSE.md for the full text.
 *
 * Platform plumbing only. Everything worth reading is in monitor.cpp.
 */

#include "monitor.hpp"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_win32.h"

#include <windows.h>

#include <GL/gl.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{

HDC g_deviceContext = nullptr;
HGLRC g_glContext = nullptr;

/**
 * Tell Windows we place our own pixels. Without this the desktop scales the
 * window as a bitmap on any display above 100%, which makes every glyph and
 * every one-pixel waveform line blurry. Resolved at run time so the program
 * still starts on Windows versions that lack the newer entry points.
 */
void becomeDpiAware()
{
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll"))
    {
        using SetContextFn = BOOL(WINAPI *)(HANDLE);
        if (auto setContext = (SetContextFn)(void *)GetProcAddress(user32, "SetProcessDpiAwarenessContext"))
        {
            /* -4: per monitor aware v2 */
            if (setContext((HANDLE)-4))
            {
                return;
            }
        }
        using SetAwareFn = BOOL(WINAPI *)(void);
        if (auto setAware = (SetAwareFn)(void *)GetProcAddress(user32, "SetProcessDPIAware"))
        {
            setAware();
        }
    }
}

/** Scale the interface so it stays the same physical size on a dense display. */
void applyDpiScale(HWND window)
{
    UINT dpi = 96;
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll"))
    {
        using GetDpiFn = UINT(WINAPI *)(HWND);
        if (auto getDpi = (GetDpiFn)(void *)GetProcAddress(user32, "GetDpiForWindow"))
        {
            const UINT reported = getDpi(window);
            if (reported >= 72)
            {
                dpi = reported;
            }
        }
    }

    const float scale = (float)dpi / 96.0f;
    if (scale > 1.01f)
    {
        ImGui::GetStyle().ScaleAllSizes(scale);
        ImGui::GetIO().FontGlobalScale = scale;
    }
}

bool createGlContext(HWND window)
{
    g_deviceContext = GetDC(window);

    PIXELFORMATDESCRIPTOR descriptor = {};
    descriptor.nSize = sizeof descriptor;
    descriptor.nVersion = 1;
    descriptor.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    descriptor.iPixelType = PFD_TYPE_RGBA;
    descriptor.cColorBits = 32;
    descriptor.cDepthBits = 24;
    descriptor.cStencilBits = 8;

    const int format = ChoosePixelFormat(g_deviceContext, &descriptor);
    if (format == 0 || !SetPixelFormat(g_deviceContext, format, &descriptor))
    {
        return false;
    }

    g_glContext = wglCreateContext(g_deviceContext);
    if (!g_glContext)
    {
        return false;
    }
    return wglMakeCurrent(g_deviceContext, g_glContext) != FALSE;
}

void destroyGlContext(HWND window)
{
    wglMakeCurrent(nullptr, nullptr);
    if (g_glContext)
    {
        wglDeleteContext(g_glContext);
        g_glContext = nullptr;
    }
    if (g_deviceContext)
    {
        ReleaseDC(window, g_deviceContext);
        g_deviceContext = nullptr;
    }
}

LRESULT WINAPI windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam))
    {
        return 1;
    }

    switch (message)
    {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
        {
            glViewport(0, 0, LOWORD(lParam), HIWORD(lParam));
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) /* swallow the alt menu */
        {
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

/* WinMain rather than wWinMain: MinGW links the wide entry point only with
   -municode, and this program takes no command line either way. */
int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int)
{
    becomeDpiAware();

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof windowClass;
    windowClass.style = CS_OWNDC;
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.lpszClassName = L"Z80MonitorWindow";
    RegisterClassExW(&windowClass);

    HWND window = CreateWindowW(windowClass.lpszClassName, L"z80mon - Z80 core monitor", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1280, 860, nullptr, nullptr, instance, nullptr);
    if (!window || !createGlContext(window))
    {
        MessageBoxW(nullptr, L"Could not create an OpenGL window.", L"z80mon", MB_ICONERROR);
        UnregisterClassW(windowClass.lpszClassName, instance);
        return 1;
    }

    ShowWindow(window, SW_SHOWDEFAULT);
    UpdateWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    applyDpiScale(window);
    ImGui_ImplWin32_InitForOpenGL(window);
    ImGui_ImplOpenGL3_Init();

    Monitor monitor;

    LARGE_INTEGER frequency;
    LARGE_INTEGER previous;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&previous);

    bool quit = false;
    while (!quit)
    {
        MSG message;
        while (PeekMessage(&message, nullptr, 0u, 0u, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessage(&message);
            if (message.message == WM_QUIT)
            {
                quit = true;
            }
        }
        if (quit)
        {
            break;
        }

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        const float seconds = (float)((double)(now.QuadPart - previous.QuadPart) / (double)frequency.QuadPart);
        previous = now;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

        monitor.frame(seconds);

        ImGui::Render();
        glClearColor(0.10f, 0.11f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SwapBuffers(g_deviceContext);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    destroyGlContext(window);
    DestroyWindow(window);
    UnregisterClassW(windowClass.lpszClassName, instance);
    return 0;
}
