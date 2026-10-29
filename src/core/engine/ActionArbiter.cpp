// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/engine/ActionArbiter.h"

#include <algorithm>

namespace nutty {

ActionArbiter::ActionArbiter(ActionSettings settings)
    : m_settings(settings)
{
}

qint64 ActionArbiter::countdownFor(const RuleTrigger& trigger, const ActionSettings& settings)
{
    if (trigger.fsd)
        return std::max(0, settings.fsdDelaySec);
    qint64 countdown = std::max(0, trigger.rule.countdownSec);
    if (trigger.runtime)
        countdown
            = std::min<qint64>(countdown, std::max<qint64>(0, *trigger.runtime - settings.safetyMarginSec));
    return countdown;
}

PendingAction ActionArbiter::makePending(const RuleTrigger& trigger, const QDateTime& now) const
{
    PendingAction p;
    p.ruleId = trigger.rule.id;
    p.ruleName = trigger.rule.name;
    p.deviceId = trigger.deviceId;
    p.deviceName = trigger.deviceName;
    p.targetName = trigger.targetName;
    p.type = trigger.rule.action;
    p.scheduledAt = now;
    p.deadline = now.addSecs(countdownFor(trigger, m_settings));
    p.cancellable = !trigger.fsd;
    p.fsd = trigger.fsd;
    p.command = trigger.rule.command;
    p.reason = trigger.reason;
    p.episodeKey = trigger.episodeKey;
    return p;
}

ActionArbiter::Offer ActionArbiter::offer(const PendingAction& candidate, const QDateTime& now)
{
    if (!candidate.fsd && !candidate.dryRun && isSnoozed(now))
        return Offer::Snoozed;

    if (!m_pending) {
        m_pending = candidate;
        return Offer::Accepted;
    }

    auto& p = *m_pending;
    // A real action always replaces a simulation.
    if (p.dryRun && !candidate.dryRun) {
        m_pending = candidate;
        return Offer::Accepted;
    }
    if (candidate.dryRun && !p.dryRun)
        return Offer::Ignored;

    const bool stronger = candidate.type > p.type;
    const bool earlier = candidate.type == p.type && candidate.deadline < p.deadline;
    const bool becomesFsd = candidate.fsd && !p.fsd;
    if (!stronger && !earlier && !becomesFsd)
        return Offer::Ignored;

    // The stronger action's details win; the deadline is the earlier one, and
    // a forced shutdown makes the result urgent and uncancellable.
    PendingAction merged = stronger ? candidate : p;
    merged.type = std::max(p.type, candidate.type);
    merged.deadline = std::min(p.deadline, candidate.deadline);
    merged.cancellable = p.cancellable && candidate.cancellable;
    merged.fsd = p.fsd || candidate.fsd;
    if (becomesFsd && !stronger)
        merged.reason = candidate.reason;
    merged.scheduledAt = p.scheduledAt;
    m_pending = merged;
    return Offer::Escalated;
}

std::optional<PendingAction> ActionArbiter::takeDue(const QDateTime& now)
{
    if (!m_pending || now < m_pending->deadline)
        return std::nullopt;
    auto due = m_pending;
    m_pending.reset();
    return due;
}

std::optional<PendingAction> ActionArbiter::cancel()
{
    if (!m_pending || !m_pending->cancellable)
        return std::nullopt;
    auto cancelled = m_pending;
    m_pending.reset();
    return cancelled;
}

std::optional<PendingAction> ActionArbiter::cancelIf(
    const std::function<bool(const PendingAction&)>& predicate)
{
    if (!m_pending || m_pending->fsd || !predicate(*m_pending))
        return std::nullopt;
    auto cancelled = m_pending;
    m_pending.reset();
    return cancelled;
}

}  // namespace nutty
