// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/window/WindowEffects.h"

namespace nutty::WindowEffects {

// Blur behind windows is up to the compositor (KWin's blur effect needs a
// private protocol, GNOME has none), so the UI draws its own background.
bool apply(QWindow*, const WindowEffectOptions&)
{
    return false;
}

}  // namespace nutty::WindowEffects
