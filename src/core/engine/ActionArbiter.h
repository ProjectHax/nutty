// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/engine/RuleEngine.h"
#include "core/model/Config.h"

#include <QDateTime>
#include <QString>
#include <QUuid>

#include <algorithm>
#include <functional>
#include <optional>

namespace nutty {

// A power action waiting for its countdown to run out.
struct PendingAction {
    QUuid ruleId;
    QString ruleName;
    QUuid deviceId;
    QString deviceName;
    QString targetName;  // device or group name the rule is about
    ActionType type = ActionType::Shutdown;
    QDateTime scheduledAt;
    QDateTime deadline;
    bool cancellable = true;  // false for forced shutdowns
    bool fsd = false;
    bool dryRun = false;  // a simulation: logged, never executed
    QString command;  // for ActionType::Command
    QString reason;  // translated, why it was scheduled
    QString episodeKey;  // empty = not tied to an outage (simulations)

    qint64 remainingSec(const QDateTime& now) const { return std::max<qint64>(0, now.secsTo(deadline)); }
};

// Decides what happens when rules fire: one pending action at a time, the
// strongest action wins, and stronger rules escalate a pending action while
// keeping the earlier deadline. Pure: callers pass the time.
class ActionArbiter
{
public:
    enum class Offer {
        Accepted,  // nothing was pending; this is now the pending action
        Escalated,  // merged into the pending action (stronger type and/or earlier deadline)
        Ignored,  // a stronger action is already pending
        Snoozed,  // actions are snoozed; offer again later
    };

    explicit ActionArbiter(ActionSettings settings = {});
    void setSettings(const ActionSettings& settings) { m_settings = settings; }

    // The countdown a trigger gets: the rule's, capped so it never runs past
    // the remaining runtime minus the safety margin; FSD gets fsdDelaySec.
    static qint64 countdownFor(const RuleTrigger& trigger, const ActionSettings& settings);
    PendingAction makePending(const RuleTrigger& trigger, const QDateTime& now) const;

    Offer offer(const PendingAction& candidate, const QDateTime& now);

    // The pending action, once its deadline has passed (and it is cleared).
    std::optional<PendingAction> takeDue(const QDateTime& now);
    // User cancel; false when nothing cancellable is pending.
    std::optional<PendingAction> cancel();
    // Cancels the pending action when the predicate holds for it (e.g. its
    // outage ended). FSD actions are never cancelled this way.
    std::optional<PendingAction> cancelIf(const std::function<bool(const PendingAction&)>& predicate);
    void clear() { m_pending.reset(); }

    void snooze(const QDateTime& until) { m_snoozedUntil = until; }
    void clearSnooze() { m_snoozedUntil = {}; }
    QDateTime snoozedUntil() const { return m_snoozedUntil; }
    bool isSnoozed(const QDateTime& now) const { return m_snoozedUntil.isValid() && now < m_snoozedUntil; }

    const std::optional<PendingAction>& pending() const { return m_pending; }

private:
    ActionSettings m_settings;
    std::optional<PendingAction> m_pending;
    QDateTime m_snoozedUntil;
};

}  // namespace nutty
