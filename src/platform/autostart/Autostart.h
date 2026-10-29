// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include <QString>

#include <expected>

namespace nutty {

// Starting nutty at login.
//   Linux:   an XDG autostart entry (Exec points at the AppImage when run from
//            one), or the Background portal under Flatpak
//   Windows: HKCU\Software\Microsoft\Windows\CurrentVersion\Run
//   macOS:   SMAppService.mainAppService (macOS 13+)
//
// Linux and Windows start nutty with --hidden. macOS login items can't pass
// arguments, so the app should start to the menu bar when launched at login.
namespace Autostart {

bool isSupported();
bool isEnabled();
std::expected<void, QString> setEnabled(bool enabled);

}  // namespace Autostart

}  // namespace nutty
