// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/window/WindowEffects.h"

#include <QWindow>

#include <windows.h>
// dwmapi.h relies on the types from windows.h.
#include <dwmapi.h>

// Older SDKs lack the Windows 11 attributes.
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif

namespace nutty::WindowEffects {

namespace {

// Before Windows 10 20H1 the dark mode attribute had the number 19.
constexpr DWORD kDarkModeBefore20H1 = 19;

// DWM_SYSTEMBACKDROP_TYPE
constexpr int kBackdropNone = 1;
constexpr int kBackdropMica = 2;

}  // namespace

bool apply(QWindow* window, const WindowEffectOptions& options)
{
    if (!window)
        return false;
    const auto hwnd = reinterpret_cast<HWND>(window->winId());

    const BOOL dark = options.dark ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark))))
        DwmSetWindowAttribute(hwnd, kDarkModeBefore20H1, &dark, sizeof(dark));

    const int backdrop = options.translucent ? kBackdropMica : kBackdropNone;
    const bool backdropApplied
        = SUCCEEDED(DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop)));

    // Mica shows through only where the frame extends over the client area.
    const bool translucent = options.translucent && backdropApplied;
    const MARGINS margins = translucent ? MARGINS { -1, -1, -1, -1 } : MARGINS { 0, 0, 0, 0 };
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    return translucent;
}

TitleBarOverlay titleBarOverlay(QWindow*)
{
    return {};
}

}  // namespace nutty::WindowEffects
