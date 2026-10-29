// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "app/viewmodels/Summary.h"

#include "app/viewmodels/ActionController.h"
#include "app/viewmodels/AppContext.h"
#include "app/viewmodels/DeviceInfo.h"
#include "core/engine/EventText.h"
#include "core/engine/Monitor.h"

#include <QLocale>

namespace nutty {

Summary::Summary(AppContext* context, QObject* parent)
    : QObject(parent)
    , m_context(context ? context : AppContext::instance())
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(0);
    connect(&m_debounce, &QTimer::timeout, this, &Summary::update);
    m_ticker.setInterval(1000);
    connect(&m_ticker, &QTimer::timeout, this, &Summary::update);
    if (m_context) {
        Monitor* monitor = m_context->monitor();
        connect(monitor, &Monitor::devicesChanged, this, &Summary::scheduleUpdate);
        connect(monitor, &Monitor::deviceStatusChanged, this, &Summary::scheduleUpdate);
        connect(monitor, &Monitor::deviceConnectionChanged, this, &Summary::scheduleUpdate);
        connect(monitor, &Monitor::pendingActionChanged, this, &Summary::scheduleUpdate);
        connect(monitor, &Monitor::snoozeChanged, this, &Summary::scheduleUpdate);
        connect(m_context, &AppContext::configChanged, this, &Summary::scheduleUpdate);
    }
    update();
}

Summary* Summary::create(QQmlEngine*, QJSEngine*)
{
    return new Summary(AppContext::instance());
}

void Summary::scheduleUpdate()
{
    m_debounce.start();
}

void Summary::update()
{
    QString level = QStringLiteral("ok");
    QString title;
    QString subtitle;
    QString icon;
    QString actionText;
    QString actionKey;
    QString health = QStringLiteral("unknown");
    int total = 0, online = 0, onBattery = 0, offline = 0, attention = 0;

    if (m_context) {
        const Monitor& monitor = *m_context->monitor();
        const QList<QUuid> ids = monitor.deviceIds();
        using H = Monitor::Health;
        // The devices in the worst state, for naming them in the title.
        QList<QUuid> worst;
        H worstHealth = H::Ok;
        int unknown = 0;
        for (const QUuid& id : ids) {
            const DeviceConfig* d = monitor.device(id);
            if (!d || !d->enabled)
                continue;
            ++total;
            const H h = monitor.health(id);
            switch (h) {
            case H::Ok:
                ++online;
                break;
            case H::Unknown:
                ++unknown;
                break;
            case H::Warning:
                ++attention;
                break;
            case H::Offline:
                ++offline;
                break;
            case H::OnBattery:
            case H::Low:
            case H::Critical:
                ++onBattery;
                break;
            }
            if (worst.isEmpty() || h > worstHealth) {
                worst = { id };
                worstHealth = h;
            } else if (h == worstHealth) {
                worst.append(id);
            }
        }
        health = total > 0 ? deviceinfo::healthKey(monitor.overallHealth()) : QStringLiteral("unknown");

        const auto nameOf = [&](const QUuid& id) {
            const DeviceConfig* d = monitor.device(id);
            return d ? text::deviceName(*d) : QString();
        };
        const auto summaryOf = [&](const QUuid& id) {
            return text::summary(monitor.status(id).charge, deviceinfo::runtime(monitor, id));
        };
        const int n = static_cast<int>(worst.size());
        const QUuid first = worst.value(0);

        if (const auto p = monitor.pendingAction()) {
            level = QStringLiteral("critical");
            icon = QStringLiteral("power");
            const bool simulation = p->dryRun || monitor.dryRun();
            title = ActionController::countdownTitle(
                enumKey(p->type).toLower(), static_cast<int>(p->remainingSec(monitor.now())), simulation);
            QString reason = p->reason;
            if (!reason.isEmpty())
                reason[0] = reason[0].toUpper();
            subtitle = p->targetName.isEmpty() ? reason : tr("%1 · %2").arg(p->targetName, reason);
            if (p->cancellable && !p->fsd) {
                actionText = tr("Cancel");
                actionKey = QStringLiteral("cancel");
            }
        } else if (m_context->config().devices.isEmpty()) {
            level = QStringLiteral("info");
            icon = QStringLiteral("plus");
            title = tr("No devices yet");
            subtitle = tr("Add a UPS, power station or battery to start monitoring it.");
            actionText = tr("Add device");
            actionKey = QStringLiteral("addDevice");
        } else if (total == 0) {
            level = QStringLiteral("info");
            icon = QStringLiteral("pause");
            title = tr("Monitoring is paused");
            subtitle = tr("Every device is disabled.");
        } else {
            switch (worstHealth) {
            case H::Critical:
                level = QStringLiteral("critical");
                title = n == 1 ? tr("%1 asks for a forced shutdown").arg(nameOf(first))
                               : tr("%n devices ask for a forced shutdown", "", n);
                subtitle = summaryOf(first);
                break;
            case H::Low:
                level = QStringLiteral("critical");
                icon = QStringLiteral("battery-low");
                title = n == 1 ? tr("%1 is running low").arg(nameOf(first))
                               : tr("%n devices are running low", "", n);
                subtitle = summaryOf(first);
                break;
            case H::OnBattery: {
                level = QStringLiteral("warning");
                title = n == 1 ? tr("%1 is on battery").arg(nameOf(first))
                               : tr("%n devices are on battery", "", n);
                subtitle = summaryOf(first);
                if (const auto since = monitor.episodeStart(first))
                    subtitle = subtitle.isEmpty()
                        ? tr("On battery for %1.").arg(text::duration(since->secsTo(monitor.now())))
                        : tr("%1 · on battery for %2")
                              .arg(subtitle, text::duration(since->secsTo(monitor.now())));
                break;
            }
            case H::Offline: {
                level = QStringLiteral("info");
                icon = QStringLiteral("link-off");
                title = n == 1 ? tr("%1 is offline").arg(nameOf(first)) : tr("%n devices are offline", "", n);
                const QDateTime seen = monitor.lastUpdate(first);
                subtitle = seen.isValid()
                    ? tr("Last heard from %1.")
                          .arg(QLocale().toString(seen.toLocalTime(), QLocale::ShortFormat))
                    : monitor.lastError(first);
                break;
            }
            case H::Warning: {
                level = QStringLiteral("warning");
                icon = QStringLiteral("warning");
                title = n == 1 ? tr("%1 needs attention").arg(nameOf(first))
                               : tr("%n devices need attention", "", n);
                const DeviceStatus s = monitor.status(first);
                if (s.has(StatusFlag::ReplaceBattery))
                    subtitle = tr("The battery should be replaced.");
                else if (s.has(StatusFlag::Overload))
                    subtitle = tr("The load is higher than the UPS can carry.");
                else if (s.has(StatusFlag::Bypass))
                    subtitle = tr("The UPS is on bypass: the battery won't take over.");
                else if (s.has(StatusFlag::Off))
                    subtitle = tr("The UPS output is off.");
                else if (!s.alarmText.isEmpty())
                    subtitle = s.alarmText;
                break;
            }
            case H::Unknown:
                level = QStringLiteral("info");
                icon = QStringLiteral("refresh");
                title = tr("Connecting to your devices");
                subtitle = tr("Waiting for the first readings.");
                break;
            case H::Ok:
                title = tr("Everything is on line power");
                subtitle = unknown > 0
                    ? tr("%n device(s) on mains power, still waiting for others.", "", online)
                    : tr("%n device(s) on mains power.", "", online);
                break;
            }
            if (n == 1 && worstHealth != H::Ok && worstHealth != H::Unknown) {
                actionText = tr("Details");
                actionKey = QStringLiteral("device:") + idString(first);
            }
        }

        QStringList notes;
        if (monitor.snoozedUntil().isValid() && monitor.snoozedUntil() > monitor.now())
            notes << tr("Power actions are snoozed until %1.")
                         .arg(QLocale().toString(
                             monitor.snoozedUntil().toLocalTime().time(), QLocale::ShortFormat));
        if (monitor.dryRun())
            notes << tr("Dry run: power actions are only simulated.");
        if (!notes.isEmpty())
            subtitle = subtitle.isEmpty() ? notes.join(u' ')
                                          : subtitle + QStringLiteral("  ·  ") + notes.join(u' ');

        if (monitor.pendingAction())
            m_ticker.start();
        else
            m_ticker.stop();
    }

    if (icon.isEmpty())
        icon = level == u"ok"      ? QStringLiteral("check")
            : level == u"critical" ? QStringLiteral("warning")
                                   : QStringLiteral("bolt");

    const bool same = level == m_level && title == m_title && subtitle == m_subtitle && icon == m_icon
        && actionText == m_actionText && actionKey == m_actionKey && health == m_health
        && total == m_deviceCount && online == m_online && onBattery == m_onBattery && offline == m_offline
        && attention == m_attention;
    if (same)
        return;
    m_level = level;
    m_title = title;
    m_subtitle = subtitle;
    m_icon = icon;
    m_actionText = actionText;
    m_actionKey = actionKey;
    m_health = health;
    m_deviceCount = total;
    m_online = online;
    m_onBattery = onBattery;
    m_offline = offline;
    m_attention = attention;
    emit changed();
}

}  // namespace nutty
