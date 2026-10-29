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

}  // namespace WindowEffects

}  // namespace nutty
