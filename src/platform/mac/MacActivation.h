// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

namespace nutty::MacActivation {

// macOS: true shows the Dock icon and menu bar while a window is open
// (NSApplicationActivationPolicyRegular) and brings the app forward; false
// makes nutty a menu bar-only accessory app. A no-op elsewhere.
void setRegularActivationPolicy(bool regular);

}  // namespace nutty::MacActivation
