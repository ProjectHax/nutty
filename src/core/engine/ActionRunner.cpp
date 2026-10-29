// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/engine/ActionRunner.h"

#include "core/engine/EventText.h"
#include "core/engine/Monitor.h"
#include "core/storage/HistoryStore.h"

#include <QTimer>

namespace nutty {

ActionRunner::ActionRunner(Monitor* monitor, HistoryStore* history, Backend backend, QObject* parent)
    : QObject(parent)
    , m_monitor(monitor)
    , m_history(history)
    , m_backend(std::move(backend))
{
    connect(m_monitor, &Monitor::actionDue, this, &ActionRunner::run);
}

void ActionRunner::setShutdownFallback(std::chrono::milliseconds after)
{
    m_shutdownFallback = after;
}

void ActionRunner::run(const PendingAction& action)
{
    if (m_monitor->dryRun() || action.dryRun) {
        m_monitor->reportActionResult(action, true);
        return;
    }
    if (m_history)
        m_history->flush();
    Result result;
    switch (action.type) {
    case ActionType::Notify:
        break;
    case ActionType::Command:
        result = m_backend.runCommand(action.command);
        break;
    case ActionType::Sleep:
    case ActionType::Hibernate:
    case ActionType::Shutdown:
        result = m_backend.execute(action.type);
        break;
    }
    m_monitor->reportActionResult(action, result.has_value(), result ? QString() : result.error());
    if (!result || action.type != ActionType::Shutdown)
        return;
    // The OS accepted the shutdown: let NUT secondaries log out now.
    m_monitor->prepareSourcesForShutdown();
    if (m_shutdownFallback <= std::chrono::milliseconds::zero())
        return;
    QTimer::singleShot(m_shutdownFallback, this, [this, action] {
        const auto sleep = m_backend.execute(ActionType::Sleep);
        const auto waited
            = text::duration(std::chrono::duration_cast<std::chrono::seconds>(m_shutdownFallback).count());
        m_monitor->reportActionResult(action, false,
            sleep
                ? tr("The computer did not shut down within %1, so nutty put it to sleep instead").arg(waited)
                : tr("The computer did not shut down within %1, and sleep failed too: %2")
                      .arg(waited, sleep.error()));
    });
}

}  // namespace nutty
