// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/engine/Monitor.h"
#include "core/model/Config.h"

#include <QString>
#include <QUuid>

// How a device is presented: the state key the QML components understand,
// its label, the artwork and a subtitle. Shared by the view-models and the
// tray so both describe a device the same way.
namespace nutty::deviceinfo {

// One of the StatusChip keys: online, charging, onBattery, lowBattery, fsd,
// bypass, overload, replaceBattery, offline, stale, error, unknown.
QString statusKey(const Monitor& monitor, const QUuid& id);
// "On battery", "Offline", "Disabled", ...
QString statusText(const Monitor& monitor, const QUuid& id);
// The artwork key (DeviceArt.kind): the configured kind, or a guess.
QString kindKey(const DeviceConfig& device, const DeviceStatus& status);
// "NUT · ups@server", "EcoFlow · R331...", "This computer".
QString subtitle(const Monitor& monitor, const DeviceConfig& device);
// "NUT", "apcupsd", "EcoFlow", "HTTP / JSON", "This computer".
QString typeName(SourceType type);
// The best runtime estimate: the device's own, counted down since it was reported.
std::optional<qint64> runtime(const Monitor& monitor, const QUuid& id);
// "Rack UPS: 63 %, 21 min left · on battery" for the tray tooltip.
QString trayLine(const Monitor& monitor, const QUuid& id);
// Health as the key the UI uses: ok, unknown, warning, offline, onBattery, low, critical.
QString healthKey(Monitor::Health health);

}  // namespace nutty::deviceinfo
