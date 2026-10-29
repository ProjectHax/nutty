// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Config.h"
#include "core/model/DeviceStatus.h"
#include "core/model/Event.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QUuid>

#include <optional>

namespace nutty {

// Everything the engine knows about one device between polls. Maintained by
// EventDetector, read by RuleEngine and Monitor.
struct DeviceState {
    QUuid id;
    bool hasData = false;
    DeviceStatus status;  // the last fresh status
    QDateTime lastDataAt;  // when it arrived
    QDateTime trackingSince;  // first seen, or last resume: the deadtime base before data arrives
    SourceError lastError = SourceError::None;
    QString lastErrorText;
    bool commLost = false;
    QDateTime commLostSince;
    bool authFailed = false;
    bool awaitingFreshData = false;  // set on resume, cleared by the next status

    // On-battery episode. It starts when the device goes on battery and ends
    // only after line power has held for ActionSettings::stableOnlineSec.
    bool inEpisode = false;
    int episodeSerial = 0;  // increments with every new episode
    QDateTime episodeStart;
    QDateTime onlineSince;  // set while back on line inside an episode
    bool lowBatteryLatched = false;
    bool chargeBelowNotified = false;
    bool runtimeBelowNotified = false;

    int fsdSerial = 0;  // increments every time FSD is raised
    StatusFlags lastFlags;
    QString lastAlarm;

    bool onBatteryNow() const { return inEpisode && !onlineSince.isValid(); }
    bool fsdActive() const { return hasData && status.has(StatusFlag::ForcedShutdown); }
    // The last known runtime minus the time since it was reported.
    std::optional<qint64> estimatedRuntime(const QDateTime& now) const;
};

// Seconds without fresh data before a device counts as lost: the configured
// deadtime, but at least three poll intervals (as upsmon requires).
int effectiveDeadTimeSec(const ActionSettings& settings, const DeviceConfig& device);

// Turns the stream of statuses and errors from each source into events, with
// debouncing: outages are episodes, so a flickering line produces one
// OnBattery and one PowerRestored instead of a storm. Pure: callers pass the
// time explicitly.
class EventDetector
{
public:
    explicit EventDetector(ActionSettings settings = {});

    void setSettings(const ActionSettings& settings) { m_settings = settings; }
    const ActionSettings& settings() const { return m_settings; }

    QList<Event> onStatus(const DeviceConfig& device, const DeviceStatus& status, const QDateTime& now);
    QList<Event> onError(
        const DeviceConfig& device, SourceError kind, const QString& message, const QDateTime& now);
    // Time-based transitions: CommLost after the deadtime, episode end after
    // stable line power. Call about once a second.
    QList<Event> tick(const DeviceConfig& device, const QDateTime& now);

    // After resume from sleep: every device must deliver fresh data again, and
    // the deadtime and stable-line timers restart.
    void markResumed(const QDateTime& now);

    void remove(const QUuid& device) { m_states.remove(device); }
    void clear() { m_states.clear(); }

    const DeviceState* state(const QUuid& device) const;
    const QHash<QUuid, DeviceState>& states() const { return m_states; }

    int deadTimeSec(const DeviceConfig& device) const { return effectiveDeadTimeSec(m_settings, device); }

private:
    DeviceState& stateFor(const DeviceConfig& device, const QDateTime& now);
    void checkEpisodeEnd(
        const DeviceConfig& device, DeviceState& state, const QDateTime& now, QList<Event>& out);

    ActionSettings m_settings;
    QHash<QUuid, DeviceState> m_states;
};

}  // namespace nutty
