// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include <QObject>

#include <memory>

namespace nutty {

// Reports system suspend and resume, so the monitor can drop stale data and
// hold off on rules until fresh readings arrive after wake.
//   Linux:   logind PrepareForSleep
//   Windows: PowerRegisterSuspendResumeNotification (also covers Modern Standby)
//   macOS:   NSWorkspace will-sleep / did-wake
class SleepWatcher : public QObject
{
    Q_OBJECT

public:
    explicit SleepWatcher(QObject* parent = nullptr);
    ~SleepWatcher() override;

    // False when the OS hook couldn't be installed (no logind, ...).
    bool isSupported() const;

    // Platform callbacks funnel through here; each signal fires once per
    // transition even if the OS reports it twice.
    void notifySleeping();
    void notifyResumed();

signals:
    void aboutToSleep();
    void resumed();

private:
    struct Private;
    std::unique_ptr<Private> d;
    bool m_sleeping = false;
};

}  // namespace nutty
