// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/power/SleepWatcher.h"

// The shared half; the constructor, destructor and isSupported() live in
// SleepWatcher_{linux,win,mac}.

namespace nutty {

void SleepWatcher::notifySleeping()
{
    if (m_sleeping)
        return;
    m_sleeping = true;
    emit aboutToSleep();
}

void SleepWatcher::notifyResumed()
{
    if (!m_sleeping)
        return;
    m_sleeping = false;
    emit resumed();
}

}  // namespace nutty
