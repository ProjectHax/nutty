// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/engine/RuleEngine.h"

#include "core/engine/EventText.h"

#include <QCoreApplication>
#include <QStringList>

#include <algorithm>

namespace nutty {

namespace {

struct Match {
    QString reason;
    std::optional<qint64> runtime;
};

QString idText(const QUuid& id)
{
    return id.toString(QUuid::WithoutBraces);
}

std::optional<Match> matchDevice(const Rule& rule, const DeviceConfig& device, const DeviceState* s,
    const ActionSettings& settings, const QDateTime& now, bool inGrace)
{
    if (!s)
        return std::nullopt;

    const bool fsdRule = rule.condition == ConditionType::ForcedShutdown;
    const bool commRule = rule.condition == ConditionType::CommLostOnBattery;
    // Right after a resume nothing is trusted until the grace period is over
    // and the device has reported again. FSD skips the grace (fresh data
    // proves it is current); a device that stays silent after the resume can
    // still count as lost once a full deadtime has passed.
    if (inGrace && !fsdRule)
        return std::nullopt;
    if (s->awaitingFreshData) {
        if (!commRule || s->trackingSince.secsTo(now) < effectiveDeadTimeSec(settings, device))
            return std::nullopt;
    }

    const auto& st = s->status;
    switch (rule.condition) {
    case ConditionType::OnBatteryFor: {
        if (!s->onBatteryNow())
            return std::nullopt;
        const qint64 onBattery = s->episodeStart.secsTo(now);
        if (onBattery < rule.threshold)
            return std::nullopt;
        return Match {
            QCoreApplication::translate("nutty::Events", "on battery for %1").arg(text::duration(onBattery)),
            st.runtime
        };
    }
    case ConditionType::ChargeBelow:
        if (!s->onBatteryNow() || !st.charge || *st.charge > rule.threshold)
            return std::nullopt;
        return Match {
            QCoreApplication::translate("nutty::Events", "battery at %1").arg(text::percent(*st.charge)),
            st.runtime
        };
    case ConditionType::RuntimeBelow:
        if (!s->onBatteryNow() || !st.runtime || *st.runtime > rule.threshold)
            return std::nullopt;
        return Match { QCoreApplication::translate("nutty::Events", "%1 of runtime left")
                           .arg(text::duration(*st.runtime)),
            st.runtime };
    case ConditionType::LowBattery:
        if (!s->inEpisode || !s->lowBatteryLatched)
            return std::nullopt;
        return Match { QCoreApplication::translate("nutty::Events", "low battery"), st.runtime };
    case ConditionType::ForcedShutdown:
        if (!s->fsdActive())
            return std::nullopt;
        return Match { QCoreApplication::translate(
                           "nutty::Events", "forced shutdown ordered by the UPS server"),
            st.runtime };
    case ConditionType::CommLostOnBattery: {
        if (!device.commLostCritical || !s->commLost || !s->inEpisode)
            return std::nullopt;
        const auto estimate = s->estimatedRuntime(now);
        if (rule.threshold > 0 && estimate && *estimate > rule.threshold)
            return std::nullopt;
        return Match { QCoreApplication::translate("nutty::Events", "contact lost while on battery"),
            estimate };
    }
    }
    return std::nullopt;
}

std::optional<qint64> minRuntime(std::optional<qint64> a, std::optional<qint64> b)
{
    if (!a)
        return b;
    if (!b)
        return a;
    return std::min(*a, *b);
}

}  // namespace

void RuleEngine::updateGroupEpisodes(const AppConfig& config, const QHash<QUuid, DeviceState>& states)
{
    for (const auto& group : config.groups) {
        bool anyInEpisode = false;
        for (const auto& member : group.members) {
            const auto it = states.constFind(member);
            if (it != states.cend() && it->inEpisode) {
                anyInEpisode = true;
                break;
            }
        }
        auto& episode = m_groupEpisodes[group.id];
        if (anyInEpisode && !episode.active) {
            episode.active = true;
            ++episode.serial;
        } else if (!anyInEpisode && episode.active) {
            episode.active = false;
        }
    }
}

QList<RuleTrigger> RuleEngine::evaluate(const AppConfig& config, const QHash<QUuid, DeviceState>& states,
    const QDateTime& now, const QDateTime& wakeGraceUntil)
{
    updateGroupEpisodes(config, states);

    QList<RuleTrigger> out;
    const bool inGrace = wakeGraceUntil.isValid() && now < wakeGraceUntil;
    const auto stateOf = [&](const QUuid& id) -> const DeviceState* {
        const auto it = states.constFind(id);
        return it == states.cend() ? nullptr : &*it;
    };

    for (const auto& rule : config.rules) {
        if (!rule.enabled)
            continue;
        const bool fsd = rule.condition == ConditionType::ForcedShutdown;

        if (rule.scope == RuleScope::Device) {
            const auto* device = config.device(rule.target);
            if (!device || !device->enabled)
                continue;
            const auto* s = stateOf(device->id);
            if (!s)
                continue;
            const auto key = fsd ? QStringLiteral("f:%1:%2").arg(idText(device->id)).arg(s->fsdSerial)
                                 : QStringLiteral("d:%1:%2").arg(idText(device->id)).arg(s->episodeSerial);
            if (hasFired(rule.id, key))
                continue;
            const auto match = matchDevice(rule, *device, s, config.actions, now, inGrace);
            if (!match)
                continue;
            out.append({ rule, device->id, text::deviceName(*device), text::deviceName(*device),
                match->runtime, match->reason, key, fsd });
            continue;
        }

        // Group scopes ----------------------------------------------------------
        const auto* group = config.group(rule.target);
        if (!group)
            continue;
        QList<const DeviceConfig*> members;
        for (const auto& id : group->members) {
            const auto* d = config.device(id);
            if (d && d->enabled)
                members << d;
        }
        if (members.isEmpty())
            continue;

        QString key;
        if (fsd) {
            int serials = 0;
            for (const auto* d : members) {
                if (const auto* s = stateOf(d->id))
                    serials += s->fsdSerial;
            }
            key = QStringLiteral("gf:%1:%2").arg(idText(group->id)).arg(serials);
        } else {
            key = QStringLiteral("g:%1:%2")
                      .arg(idText(group->id))
                      .arg(m_groupEpisodes.value(group->id).serial);
        }
        if (hasFired(rule.id, key))
            continue;

        QList<std::pair<const DeviceConfig*, Match>> matches;
        for (const auto* d : members) {
            if (auto m = matchDevice(rule, *d, stateOf(d->id), config.actions, now, inGrace))
                matches.append({ d, *m });
        }
        const bool fires
            = rule.scope == RuleScope::GroupAny ? !matches.isEmpty() : matches.size() == members.size();
        if (!fires)
            continue;

        std::optional<qint64> runtime;
        QStringList reasons;
        for (const auto& [d, m] : matches) {
            runtime = minRuntime(runtime, m.runtime);
            reasons << QStringLiteral("%1: %2").arg(text::deviceName(*d), m.reason);
        }
        const auto* first = matches.first().first;
        const auto groupName = group->name.trimmed().isEmpty()
            ? QCoreApplication::translate("nutty::Events", "Unnamed group")
            : group->name.trimmed();
        out.append({ rule, first->id, text::deviceName(*first), groupName, runtime,
            reasons.join(QStringLiteral("; ")), key, fsd });
    }
    return out;
}

void RuleEngine::markFired(const QUuid& ruleId, const QString& episodeKey)
{
    m_fired.insert(ruleId, episodeKey);
}

void RuleEngine::unmarkFired(const QUuid& ruleId)
{
    m_fired.remove(ruleId);
}

bool RuleEngine::hasFired(const QUuid& ruleId, const QString& episodeKey) const
{
    const auto it = m_fired.constFind(ruleId);
    return it != m_fired.cend() && *it == episodeKey;
}

bool RuleEngine::isEpisodeActive(const QString& episodeKey, const QHash<QUuid, DeviceState>& states) const
{
    const auto parts = episodeKey.split(u':');
    if (parts.size() != 3)
        return true;
    const QUuid id(parts.at(1));
    const int serial = parts.at(2).toInt();
    if (parts.at(0) == QLatin1String("d")) {
        const auto it = states.constFind(id);
        return it != states.cend() && it->inEpisode && it->episodeSerial == serial;
    }
    if (parts.at(0) == QLatin1String("g")) {
        const auto it = m_groupEpisodes.constFind(id);
        return it != m_groupEpisodes.cend() && it->active && it->serial == serial;
    }
    return true;  // FSD keys aren't tied to an outage
}

void RuleEngine::reset()
{
    m_fired.clear();
    m_groupEpisodes.clear();
}

QList<Rule> defaultRulesForDevice(const DeviceConfig& device)
{
    QList<Rule> rules;

    Rule low;
    low.id = QUuid::createUuid();
    low.name = QCoreApplication::translate("nutty::Events", "Shut down on low battery");
    low.scope = RuleScope::Device;
    low.target = device.id;
    low.condition = ConditionType::LowBattery;
    low.threshold = 0;
    low.action = ActionType::Shutdown;
    low.countdownSec = 60;
    rules << low;

    if (device.type == SourceType::Nut) {
        Rule fsd;
        fsd.id = QUuid::createUuid();
        fsd.name = QCoreApplication::translate("nutty::Events", "Shut down when the UPS server orders it");
        fsd.scope = RuleScope::Device;
        fsd.target = device.id;
        fsd.condition = ConditionType::ForcedShutdown;
        fsd.threshold = 0;
        fsd.action = ActionType::Shutdown;
        fsd.countdownSec = 0;
        rules << fsd;
    }
    return rules;
}

}  // namespace nutty
