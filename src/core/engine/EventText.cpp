// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/engine/EventText.h"

#include <QCoreApplication>
#include <QLocale>

#include <cmath>

namespace nutty::text {

QString duration(qint64 seconds)
{
    seconds = std::max<qint64>(0, seconds);
    if (seconds < 60)
        return QCoreApplication::translate("nutty::Events", "%1 s").arg(seconds);
    const qint64 minutes = (seconds + 30) / 60;
    if (minutes < 60)
        return QCoreApplication::translate("nutty::Events", "%1 min").arg(minutes);
    const qint64 hours = minutes / 60;
    const qint64 rest = minutes % 60;
    if (rest == 0)
        return QCoreApplication::translate("nutty::Events", "%1 h").arg(hours);
    return QCoreApplication::translate("nutty::Events", "%1 h %2 min").arg(hours).arg(rest);
}

QString percent(double value)
{
    return QCoreApplication::translate("nutty::Events", "%1 %")
        .arg(QLocale().toString(std::round(value), 'f', 0));
}

QString summary(const std::optional<double>& charge, const std::optional<qint64>& runtime)
{
    if (charge && runtime)
        return QCoreApplication::translate("nutty::Events", "%1, %2 left")
            .arg(percent(*charge), duration(*runtime));
    if (charge)
        return percent(*charge);
    if (runtime)
        return QCoreApplication::translate("nutty::Events", "%1 left").arg(duration(*runtime));
    return {};
}

QString summary(const DeviceStatus& status)
{
    return summary(status.charge, status.runtime);
}

QString actionName(ActionType action)
{
    switch (action) {
    case ActionType::Notify:
        return QCoreApplication::translate("nutty::Events", "Notification");
    case ActionType::Command:
        return QCoreApplication::translate("nutty::Events", "Command");
    case ActionType::Sleep:
        return QCoreApplication::translate("nutty::Events", "Sleep");
    case ActionType::Hibernate:
        return QCoreApplication::translate("nutty::Events", "Hibernate");
    case ActionType::Shutdown:
        return QCoreApplication::translate("nutty::Events", "Shutdown");
    }
    return {};
}

QString actionVerb(ActionType action)
{
    switch (action) {
    case ActionType::Notify:
        return QCoreApplication::translate("nutty::Events", "Notify");
    case ActionType::Command:
        return QCoreApplication::translate("nutty::Events", "Run command");
    case ActionType::Sleep:
        return QCoreApplication::translate("nutty::Events", "Sleep");
    case ActionType::Hibernate:
        return QCoreApplication::translate("nutty::Events", "Hibernate");
    case ActionType::Shutdown:
        return QCoreApplication::translate("nutty::Events", "Shut down");
    }
    return {};
}

QString deviceName(const DeviceConfig& device)
{
    if (!device.name.trimmed().isEmpty())
        return device.name.trimmed();
    return QCoreApplication::translate("nutty::Events", "Unnamed device");
}

QString controlName(const QString& id)
{
    struct Name {
        const char* id;
        const char* text;
    };
    static constexpr Name kNames[] = {
        { "acOutput", QT_TRANSLATE_NOOP("nutty::Events", "AC output") },
        { "xBoost", QT_TRANSLATE_NOOP("nutty::Events", "X-Boost") },
        { "acTimeout", QT_TRANSLATE_NOOP("nutty::Events", "AC timeout") },
        { "carOutput", QT_TRANSLATE_NOOP("nutty::Events", "12 V output") },
        { "acChargeSpeed", QT_TRANSLATE_NOOP("nutty::Events", "AC charging speed") },
        { "chargeLimit", QT_TRANSLATE_NOOP("nutty::Events", "Charge limit") },
        { "dischargeLimit", QT_TRANSLATE_NOOP("nutty::Events", "Discharge limit") },
        { "carInputCurrent", QT_TRANSLATE_NOOP("nutty::Events", "Car charging current") },
        { "backupReserve", QT_TRANSLATE_NOOP("nutty::Events", "Backup reserve") },
        { "backupReserveLevel", QT_TRANSLATE_NOOP("nutty::Events", "Reserve level") },
        { "beeper", QT_TRANSLATE_NOOP("nutty::Events", "Beeper") },
        { "screenTimeout", QT_TRANSLATE_NOOP("nutty::Events", "Screen timeout") },
        { "unitTimeout", QT_TRANSLATE_NOOP("nutty::Events", "Unit timeout") },
        { "generatorStart", QT_TRANSLATE_NOOP("nutty::Events", "Generator start") },
        { "generatorStop", QT_TRANSLATE_NOOP("nutty::Events", "Generator stop") },
    };
    for (const Name& n : kNames) {
        if (id == QLatin1StringView(n.id))
            return QCoreApplication::translate("nutty::Events", n.text);
    }
    return id;
}

QString controlValue(const DeviceControl& control, double value)
{
    if (control.kind == ControlKind::Toggle)
        return value != 0 ? QCoreApplication::translate("nutty::Events", "On")
                          : QCoreApplication::translate("nutty::Events", "Off");
    const QString& unit = control.unit;
    if (unit == u"s" || unit == u"min") {
        if (value == 0)
            return QCoreApplication::translate("nutty::Events", "Never");
        return duration(static_cast<qint64>(unit == u"min" ? value * 60 : value));
    }
    if (unit == u"%")
        return percent(value);
    if (unit == u"mA")
        return QCoreApplication::translate("nutty::Events", "%1 A")
            .arg(QLocale().toString(value / 1000.0, 'g', 3));
    const QString number = QLocale().toString(value, 'f', 0);
    return unit.isEmpty() ? number : QCoreApplication::translate("nutty::Events", "%1 %2").arg(number, unit);
}

Event makeEvent(EventType type, const DeviceConfig* device, const DeviceStatus* status, const QDateTime& now)
{
    Event e;
    e.timestamp = now;
    e.type = type;
    e.severity = defaultSeverity(type);
    if (device) {
        e.deviceId = device->id;
        e.deviceName = deviceName(*device);
    }
    if (status && status->isValid()) {
        e.flags = status->flags;
        e.charge = status->charge;
        e.runtime = status->runtime;
        e.load = status->load;
    }
    return e;
}

}  // namespace nutty::text
