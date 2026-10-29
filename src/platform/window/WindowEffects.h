// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

class QWindow;

namespace nutty {

struct WindowEffectOptions {
    // Ask for the OS backdrop (Mica on Windows 11, vibrancy on macOS). The
    // window's own background must then be transparent for it to show.
    bool translucent = true;
    // Dark title bar / backdrop variant.
    bool dark = false;
};

// Native window chrome for the main window.
//   Windows: dark title bar (Windows 10 1809+), Mica backdrop (Windows 11 22H2+)
//   macOS:   transparent title bar over the content, NSVisualEffectView behind it
//   Linux:   nothing; the compositor decides
namespace WindowEffects {

// Returns true when a translucent backdrop is in place, so the caller knows
// whether to clear its background. Safe to call again when options change.
bool apply(QWindow* window, const WindowEffectOptions& options);

// A native title bar drawn over the top of the content (macOS), which the
// content keeps clear for the window buttons in it.
struct TitleBarOverlay {
    int height = 0;  // 0 where the title bar sits above the content
    int buttonsWidth = 0;  // from the window's edge, with as much room past them
    bool buttonsOnRight = false;  // right-to-left systems put them there
};
TitleBarOverlay titleBarOverlay(QWindow* window);

}  // namespace WindowEffects

}  // namespace nutty
