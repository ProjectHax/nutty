// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/engine/EventDetector.h"

#include "core/engine/EventText.h"

#include <QCoreApplication>

#include <algorithm>

namespace nutty {

namespace {

// "UPS-A is on battery (87 %, 24 min left)" style: appends " (summary)" when
// there is one.
QString withSummary(const QString& sentence, const DeviceStatus& status)
{
    const auto s = text::summary(status);
    return s.isEmpty() ? sentence : QStringLiteral("%1 (%2)").arg(sentence, s);
}

}  // namespace

std::optional<qint64> DeviceState::estimatedRuntime(const QDateTime& now) const
{
    if (!hasData || !status.runtime || !lastDataAt.isValid())
        return std::nullopt;
    return std::max<qint64>(0, *status.runtime - lastDataAt.secsTo(now));
}

EventDetector::EventDetector(ActionSettings settings)
    : m_settings(settings)
{
}

const DeviceState* EventDetector::state(const QUuid& device) const
{
    const auto it = m_states.constFind(device);
    return it == m_states.cend() ? nullptr : &*it;
}

int effectiveDeadTimeSec(const ActionSettings& settings, const DeviceConfig& device)
{
    return std::max(settings.deadTimeSec, 3 * device.effectivePollIntervalSec());
}

DeviceState& EventDetector::stateFor(const DeviceConfig& device, const QDateTime& now)
{
    auto it = m_states.find(device.id);
    if (it == m_states.end()) {
        DeviceState s;
        s.id = device.id;
        s.trackingSince = now;
        it = m_states.insert(device.id, s);
    }
    return *it;
}

QList<Event> EventDetector::onStatus(
    const DeviceConfig& device, const DeviceStatus& status, const QDateTime& now)
{
    QList<Event> out;
    auto& s = stateFor(device, now);
    const auto name = text::deviceName(device);
    const StatusFlags prev = s.hasData ? s.lastFlags : StatusFlags {};
    const StatusFlags flags = status.flags;
    const StatusFlags rising = flags & ~prev;

    s.status = status;
    s.hasData = true;
    s.lastDataAt = now;
    s.awaitingFreshData = false;
    s.lastError = SourceError::None;
    s.lastErrorText.clear();
    s.authFailed = false;

    const auto event = [&](EventType type) { return text::makeEvent(type, &device, &status, now); };

    if (s.commLost) {
        s.commLost = false;
        // Line power wasn't observed while contact was lost: the stable
        // period starts over.
        s.onlineSince = {};
        auto e = event(EventType::CommRestored);
        e.title = QCoreApplication::translate("nutty::Events", "Connection restored");
        e.message = QCoreApplication::translate("nutty::Events", "Contact with %1 restored").arg(name);
        out << e;
    }

    // Episodes ----------------------------------------------------------------
    if (flags.testFlag(StatusFlag::OnBattery)) {
        if (!s.inEpisode) {
            s.inEpisode = true;
            ++s.episodeSerial;
            s.episodeStart = now;
            s.onlineSince = {};
            s.chargeBelowNotified = false;
            s.runtimeBelowNotified = false;
            s.lowBatteryLatched = false;  // a new outage announces LB again
            auto e = event(EventType::OnBattery);
            e.title = QCoreApplication::translate("nutty::Events", "On battery");
            e.message = withSummary(
                QCoreApplication::translate("nutty::Events", "%1 is on battery").arg(name), status);
            out << e;
        } else if (s.onlineSince.isValid()) {
            s.onlineSince = {};  // a blip back to line power: still the same outage
        }
    } else if (s.inEpisode && !s.onlineSince.isValid()) {
        s.onlineSince = now;
    }

    // Low battery: latched for the rest of the episode -----------------------
    if (flags.testFlag(StatusFlag::LowBattery)) {
        if (!s.lowBatteryLatched) {
            s.lowBatteryLatched = true;
            auto e = event(EventType::LowBattery);
            e.title = QCoreApplication::translate("nutty::Events", "Low battery");
            e.message = withSummary(
                QCoreApplication::translate("nutty::Events", "%1 reports a low battery").arg(name), status);
            out << e;
        }
    } else if (!s.inEpisode) {
        s.lowBatteryLatched = false;
    }

    // Notification thresholds, once per episode --------------------------------
    if (s.onBatteryNow()) {
        if (device.notifyChargeBelow > 0 && !s.chargeBelowNotified && status.charge
            && *status.charge <= device.notifyChargeBelow) {
            s.chargeBelowNotified = true;
            auto e = event(EventType::ChargeBelow);
            e.title = QCoreApplication::translate("nutty::Events", "Battery below %1")
                          .arg(text::percent(device.notifyChargeBelow));
            e.message = withSummary(
                QCoreApplication::translate("nutty::Events", "%1 is running low").arg(name), status);
            e.extra.insert(QStringLiteral("threshold"), device.notifyChargeBelow);
            out << e;
        }
        if (device.notifyRuntimeBelowSec > 0 && !s.runtimeBelowNotified && status.runtime
            && *status.runtime <= device.notifyRuntimeBelowSec) {
            s.runtimeBelowNotified = true;
            auto e = event(EventType::RuntimeBelow);
            e.title = QCoreApplication::translate("nutty::Events", "Runtime below %1")
                          .arg(text::duration(device.notifyRuntimeBelowSec));
            e.message = QCoreApplication::translate("nutty::Events", "%1 has %2 of runtime left")
                            .arg(name, text::duration(*status.runtime));
            e.extra.insert(QStringLiteral("threshold"), device.notifyRuntimeBelowSec);
            out << e;
        }
    }

    // Rising flags ----------------------------------------------------------------
    if (rising.testFlag(StatusFlag::ForcedShutdown)) {
        ++s.fsdSerial;
        auto e = event(EventType::ForcedShutdown);
        e.title = QCoreApplication::translate("nutty::Events", "Forced shutdown");
        e.message
            = QCoreApplication::translate("nutty::Events", "%1: the UPS server ordered a forced shutdown")
                  .arg(name);
        out << e;
    }
    if (rising.testFlag(StatusFlag::ReplaceBattery)) {
        auto e = event(EventType::ReplaceBattery);
        e.title = QCoreApplication::translate("nutty::Events", "Replace battery");
        e.message
            = QCoreApplication::translate("nutty::Events", "%1 reports that its battery needs replacing")
                  .arg(name);
        out << e;
    }
    if (rising.testFlag(StatusFlag::Overload)) {
        auto e = event(EventType::Overload);
        e.title = QCoreApplication::translate("nutty::Events", "Overload");
        e.message = status.load ? QCoreApplication::translate("nutty::Events", "%1 is overloaded (%2 load)")
                                      .arg(name, text::percent(*status.load))
                                : QCoreApplication::translate("nutty::Events", "%1 is overloaded").arg(name);
        out << e;
    }
    if (rising.testFlag(StatusFlag::Bypass)) {
        auto e = event(EventType::Bypass);
        e.title = QCoreApplication::translate("nutty::Events", "On bypass");
        e.message = QCoreApplication::translate(
            "nutty::Events", "%1 is on bypass; the battery is not protecting the load")
                        .arg(name);
        out << e;
    }
    if (rising.testFlag(StatusFlag::Off)) {
        auto e = event(EventType::UpsOff);
        e.title = QCoreApplication::translate("nutty::Events", "UPS off");
        e.message = QCoreApplication::translate("nutty::Events", "%1 is switched off").arg(name);
        out << e;
    }
    const auto alarm = status.alarmText.trimmed();
    if (rising.testFlag(StatusFlag::Alarm) || (!alarm.isEmpty() && alarm != s.lastAlarm)) {
        auto e = event(EventType::Alarm);
        e.title = QCoreApplication::translate("nutty::Events", "Alarm");
        e.message = alarm.isEmpty()
            ? QCoreApplication::translate("nutty::Events", "%1 raised an alarm").arg(name)
            : QCoreApplication::translate("nutty::Events", "%1 raised an alarm: %2").arg(name, alarm);
        if (!alarm.isEmpty())
            e.extra.insert(QStringLiteral("alarm"), alarm);
        out << e;
    }
    s.lastAlarm = alarm;
    s.lastFlags = flags;

    checkEpisodeEnd(device, s, now, out);
    return out;
}

QList<Event> EventDetector::onError(
    const DeviceConfig& device, SourceError kind, const QString& message, const QDateTime& now)
{
    QList<Event> out;
    auto& s = stateFor(device, now);
    s.lastError = kind;
    s.lastErrorText = message;
    if (kind == SourceError::Auth && !s.authFailed) {
        s.authFailed = true;
        auto e = text::makeEvent(EventType::AuthFailed, &device, nullptr, now);
        e.title = QCoreApplication::translate("nutty::Events", "Authentication failed");
        e.message = message.isEmpty()
            ? QCoreApplication::translate("nutty::Events", "%1 rejected the credentials")
                  .arg(text::deviceName(device))
            : QCoreApplication::translate("nutty::Events", "%1 rejected the credentials: %2")
                  .arg(text::deviceName(device), message);
        e.extra.insert(QStringLiteral("error"), message);
        out << e;
    }
    return out;
}

QList<Event> EventDetector::tick(const DeviceConfig& device, const QDateTime& now)
{
    QList<Event> out;
    auto& s = stateFor(device, now);

    // The deadtime runs from the last fresh data, or from when tracking
    // (re)started if that is later (startup, resume).
    QDateTime base = s.trackingSince;
    if (s.lastDataAt.isValid() && s.lastDataAt > base)
        base = s.lastDataAt;
    if (!s.commLost && base.isValid() && base.secsTo(now) >= deadTimeSec(device)) {
        s.commLost = true;
        s.commLostSince = now;
        auto e = text::makeEvent(EventType::CommLost, &device, s.hasData ? &s.status : nullptr, now);
        e.title = QCoreApplication::translate("nutty::Events", "Connection lost");
        const auto name = text::deviceName(device);
        e.message = s.lastErrorText.isEmpty()
            ? QCoreApplication::translate("nutty::Events", "Lost contact with %1").arg(name)
            : QCoreApplication::translate("nutty::Events", "Lost contact with %1: %2")
                  .arg(name, s.lastErrorText);
        if (!s.lastErrorText.isEmpty())
            e.extra.insert(QStringLiteral("error"), s.lastErrorText);
        if (s.inEpisode)
            e.extra.insert(QStringLiteral("onBattery"), true);
        out << e;
    }

    checkEpisodeEnd(device, s, now, out);
    return out;
}

void EventDetector::checkEpisodeEnd(
    const DeviceConfig& device, DeviceState& s, const QDateTime& now, QList<Event>& out)
{
    // Line power has to be observed (fresh data, no comm loss) for the whole
    // stable period before the outage counts as over.
    if (!s.inEpisode || !s.onlineSince.isValid() || s.commLost || s.awaitingFreshData)
        return;
    if (s.onlineSince.secsTo(now) < m_settings.stableOnlineSec)
        return;

    const qint64 onBatterySec = s.episodeStart.secsTo(s.onlineSince);
    s.inEpisode = false;
    s.onlineSince = {};
    s.chargeBelowNotified = false;
    s.runtimeBelowNotified = false;
    // LB may still be set while recharging; don't re-announce it.
    s.lowBatteryLatched = s.status.has(StatusFlag::LowBattery);

    auto e = text::makeEvent(EventType::PowerRestored, &device, &s.status, now);
    e.title = QCoreApplication::translate("nutty::Events", "Power restored");
    e.message = QCoreApplication::translate("nutty::Events", "%1 is back on line power after %2 on battery")
                    .arg(text::deviceName(device), text::duration(onBatterySec));
    e.extra.insert(QStringLiteral("onBatterySec"), onBatterySec);
    out << e;
}

void EventDetector::markResumed(const QDateTime& now)
{
    for (auto& s : m_states) {
        s.awaitingFreshData = true;
        s.trackingSince = now;
        // Whatever was seen before sleeping no longer proves line power is stable.
        if (s.inEpisode)
            s.onlineSince = {};
    }
}

}  // namespace nutty
