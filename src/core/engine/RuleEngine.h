// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/engine/EventDetector.h"
#include "core/model/Config.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QUuid>

#include <optional>

namespace nutty {

// A rule whose condition holds and that hasn't fired in its current episode.
struct RuleTrigger {
    Rule rule;
    QUuid deviceId;  // the device that matched (first match for groups)
    QString deviceName;
    QString targetName;  // the device or group the rule is about
    std::optional<qint64> runtime;  // lowest runtime among the matching devices, for the countdown cap
    QString reason;  // translated, e.g. "battery at 18 %"
    QString episodeKey;  // what "once per episode" is counted against
    bool fsd = false;  // a forced-shutdown trigger: no countdown, no cancel
};

// Evaluates the user's rules against the detector's device states. Pure:
// callers pass the time, and decide which triggers were acted on via
// markFired(), so a snoozed trigger comes back once the snooze ends.
class RuleEngine
{
public:
    QList<RuleTrigger> evaluate(const AppConfig& config, const QHash<QUuid, DeviceState>& states,
        const QDateTime& now, const QDateTime& wakeGraceUntil = {});

    void markFired(const QUuid& ruleId, const QString& episodeKey);
    // Lets a rule fire again in its current episode (e.g. its action was
    // dropped by a resume before it could run).
    void unmarkFired(const QUuid& ruleId);
    bool hasFired(const QUuid& ruleId, const QString& episodeKey) const;

    // Whether the episode a key refers to is still going. Keys that aren't
    // tied to an outage (FSD, simulations) always are.
    bool isEpisodeActive(const QString& episodeKey, const QHash<QUuid, DeviceState>& states) const;

    void reset();

private:
    struct GroupEpisode {
        bool active = false;
        int serial = 0;
    };

    void updateGroupEpisodes(const AppConfig& config, const QHash<QUuid, DeviceState>& states);

    QHash<QUuid, QString> m_fired;  // rule id -> episode key it last fired in
    QHash<QUuid, GroupEpisode> m_groupEpisodes;
};

// The rules offered for a newly added device: shut down on low battery, and
// for NUT also on a forced shutdown from the primary.
QList<Rule> defaultRulesForDevice(const DeviceConfig& device);

}  // namespace nutty
