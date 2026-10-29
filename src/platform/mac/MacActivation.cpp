// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/mac/MacActivation.h"

#include <QtGlobal>

// macOS has its own definition in MacActivation_mac.mm.
#ifndef Q_OS_MACOS

namespace nutty::MacActivation {

void setRegularActivationPolicy(bool)
{
}

}  // namespace nutty::MacActivation

#endif
