// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "app/viewmodels/DeviceInfo.h"

#include "core/engine/EventText.h"

#include <QCoreApplication>

namespace nutty::deviceinfo {

QString statusKey(const Monitor& monitor, const QUuid& id)
{
    const DeviceConfig* device = monitor.device(id);
    if (!device || !device->enabled)
        return QStringLiteral("unknown");
    const DeviceStatus status = monitor.status(id);
    if (monitor.isCommLost(id))
        return QStringLiteral("offline");
    const ConnectionState state = monitor.connectionState(id);
    if (!status.isValid())
        return state == ConnectionState::Error ? QStringLiteral("error") : QStringLiteral("unknown");
    if (status.has(StatusFlag::ForcedShutdown))
        return QStringLiteral("fsd");
    if (monitor.isStale(id) || state == ConnectionState::Stale)
        return QStringLiteral("stale");
    if (status.has(StatusFlag::LowBattery))
        return QStringLiteral("lowBattery");
    if (status.onBattery())
        return QStringLiteral("onBattery");
    if (status.has(StatusFlag::Overload))
        return QStringLiteral("overload");
    if (status.has(StatusFlag::Bypass))
        return QStringLiteral("bypass");
    if (status.has(StatusFlag::ReplaceBattery))
        return QStringLiteral("replaceBattery");
    if (state == ConnectionState::Error)
        return QStringLiteral("error");
    if (status.has(StatusFlag::Charging))
        return QStringLiteral("charging");
    if (status.online())
        return QStringLiteral("online");
    return QStringLiteral("unknown");
}

QString statusText(const Monitor& monitor, const QUuid& id)
{
    const DeviceConfig* device = monitor.device(id);
    if (device && !device->enabled)
        return QCoreApplication::translate("nutty::DeviceInfo", "Disabled");
    const QString key = statusKey(monitor, id);
    if (key == u"unknown")
        return monitor.status(id).isValid() ? QCoreApplication::translate("nutty::DeviceInfo", "Unknown")
                                            : QCoreApplication::translate("nutty::DeviceInfo", "Connecting");
    if (key == u"error") {
        switch (monitor.lastErrorKind(id)) {
        case SourceError::Auth:
            return QCoreApplication::translate("nutty::DeviceInfo", "Sign-in failed");
        case SourceError::NotFound:
            return QCoreApplication::translate("nutty::DeviceInfo", "Not found");
        case SourceError::Config:
            return QCoreApplication::translate("nutty::DeviceInfo", "Check settings");
        case SourceError::Tls:
            return QCoreApplication::translate("nutty::DeviceInfo", "TLS error");
        default:
            return QCoreApplication::translate("nutty::DeviceInfo", "Can't connect");
        }
    }
    // The other keys use StatusChip's own labels.
    return {};
}

QString kindKey(const DeviceConfig& device, const DeviceStatus& status)
{
    if (device.kind != DeviceKind::Auto)
        return enumKey(device.kind);
    const QString model = (status.manufacturer + u' ' + status.model).toLower();
    switch (device.type) {
    case SourceType::EcoFlow:
        return model.contains(u"powerwall") || model.contains(u"home") ? QStringLiteral("HomeBattery")
                                                                       : QStringLiteral("PowerStation");
    case SourceType::System:
        return status.raw.value(QStringLiteral("kind")) == u"ups" || model.contains(u"ups")
            ? QStringLiteral("TowerUps")
            : QStringLiteral("Laptop");
    case SourceType::HttpJson:
        return QStringLiteral("HomeBattery");
    case SourceType::Nut:
    case SourceType::Apcupsd:
        if (model.contains(u"rm") || model.contains(u"rack") || model.contains(u" 2u")
            || model.contains(u" 1u"))
            return QStringLiteral("RackUps");
        return QStringLiteral("TowerUps");
    }
    return QStringLiteral("Generic");
}

QString typeName(SourceType type)
{
    switch (type) {
    case SourceType::Nut:
        return QCoreApplication::translate("nutty::DeviceInfo", "NUT");
    case SourceType::Apcupsd:
        return QCoreApplication::translate("nutty::DeviceInfo", "apcupsd");
    case SourceType::EcoFlow:
        return QCoreApplication::translate("nutty::DeviceInfo", "EcoFlow");
    case SourceType::HttpJson:
        return QCoreApplication::translate("nutty::DeviceInfo", "HTTP / JSON");
    case SourceType::System:
        return QCoreApplication::translate("nutty::DeviceInfo", "This computer");
    }
    return {};
}

QString subtitle(const Monitor& monitor, const DeviceConfig& device)
{
    const DeviceStatus status = monitor.status(device.id);
    QString model = status.model;
    if (!status.manufacturer.isEmpty() && !model.startsWith(status.manufacturer, Qt::CaseInsensitive))
        model = (status.manufacturer + u' ' + model).trimmed();
    if (device.type == SourceType::System)
        return model.isEmpty() ? typeName(device.type) : model;
    QString where;
    if (const Source* source = monitor.source(device.id))
        where = source->describe();
    QStringList parts { typeName(device.type) };
    if (!model.isEmpty())
        parts << model;
    else if (!where.isEmpty())
        parts << where;
    return parts.join(QStringLiteral(" · "));
}

std::optional<qint64> runtime(const Monitor& monitor, const QUuid& id)
{
    if (const DeviceState* state = monitor.deviceState(id)) {
        if (state->hasData && state->status.onBattery())
            return state->estimatedRuntime(monitor.now());
    }
    return monitor.status(id).runtime;
}

QString trayLine(const Monitor& monitor, const QUuid& id)
{
    const DeviceConfig* device = monitor.device(id);
    if (!device)
        return {};
    const QString name = text::deviceName(*device);
    if (!device->enabled)
        return QCoreApplication::translate("nutty::DeviceInfo", "%1: disabled").arg(name);
    const QString key = statusKey(monitor, id);
    if (key == u"offline" || key == u"error")
        return QCoreApplication::translate("nutty::DeviceInfo", "%1: offline").arg(name);
    const DeviceStatus status = monitor.status(id);
    if (!status.isValid())
        return QCoreApplication::translate("nutty::DeviceInfo", "%1: connecting").arg(name);
    QString line = text::summary(status.charge, runtime(monitor, id));
    QString state;
    if (key == u"fsd")
        state = QCoreApplication::translate("nutty::DeviceInfo", "forced shutdown");
    else if (key == u"lowBattery")
        state = QCoreApplication::translate("nutty::DeviceInfo", "low battery");
    else if (key == u"onBattery")
        state = QCoreApplication::translate("nutty::DeviceInfo", "on battery");
    else if (key == u"stale")
        state = QCoreApplication::translate("nutty::DeviceInfo", "no recent data");
    else if (key == u"charging")
        state = QCoreApplication::translate("nutty::DeviceInfo", "charging");
    if (line.isEmpty())
        line = state.isEmpty() ? QCoreApplication::translate("nutty::DeviceInfo", "on line power") : state;
    else if (!state.isEmpty())
        line = QCoreApplication::translate("nutty::DeviceInfo", "%1 · %2").arg(line, state);
    return QCoreApplication::translate("nutty::DeviceInfo", "%1: %2").arg(name, line);
}

QString healthKey(Monitor::Health health)
{
    switch (health) {
    case Monitor::Health::Ok:
        return QStringLiteral("ok");
    case Monitor::Health::Unknown:
        return QStringLiteral("unknown");
    case Monitor::Health::Warning:
        return QStringLiteral("warning");
    case Monitor::Health::Offline:
        return QStringLiteral("offline");
    case Monitor::Health::OnBattery:
        return QStringLiteral("onBattery");
    case Monitor::Health::Low:
        return QStringLiteral("low");
    case Monitor::Health::Critical:
        return QStringLiteral("critical");
    }
    return QStringLiteral("unknown");
}

}  // namespace nutty::deviceinfo
