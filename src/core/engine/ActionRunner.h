// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/engine/ActionArbiter.h"
#include "core/model/Types.h"

#include <QObject>
#include <QString>

#include <chrono>
#include <expected>
#include <functional>

namespace nutty {

class HistoryStore;
class Monitor;

// Carries out the power actions the Monitor says are due: runs them through
// the backend, reports how it went back to the Monitor, and once the OS has
// accepted a shutdown lets the sources say goodbye (NUT secondaries log out).
// The backend is the platform's PowerActions in the app and a fake in tests.
class ActionRunner : public QObject
{
    Q_OBJECT

public:
    using Result = std::expected<void, QString>;

    struct Backend {
        std::function<Result(ActionType)> execute;  // Sleep, Hibernate or Shutdown
        std::function<Result(const QString&)> runCommand;
    };

    // history may be null; it's flushed before anything runs, so an outage
    // that ends with the machine going down is on disk.
    ActionRunner(Monitor* monitor, HistoryStore* history, Backend backend, QObject* parent = nullptr);

    // If the machine is still running this long after the OS accepted a
    // shutdown, put it to sleep instead and report the shutdown as failed.
    // Zero (the default) waits forever. macOS needs it: an app that refuses
    // to quit stops a shutdown through System Events.
    void setShutdownFallback(std::chrono::milliseconds after);

    // Runs a due action. Connected to Monitor::actionDue.
    void run(const PendingAction& action);

private:
    Monitor* m_monitor;
    HistoryStore* m_history;
    Backend m_backend;
    std::chrono::milliseconds m_shutdownFallback { 0 };
};

}  // namespace nutty
