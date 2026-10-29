// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Config.h"
#include "core/model/DeviceControl.h"
#include "core/model/DeviceStatus.h"
#include "core/model/Event.h"

#include <QDateTime>
#include <QString>

#include <optional>

// Human-readable text for events and actions. Every string is translated in
// the "nutty::Events" context.
namespace nutty::text {

// "45 s", "24 min", "1 h 5 min".
QString duration(qint64 seconds);
// "87 %".
QString percent(double value);
// "87 %, 24 min left" (whatever is known; empty when nothing is).
QString summary(const std::optional<double>& charge, const std::optional<qint64>& runtime);
QString summary(const DeviceStatus& status);
// "Shutdown", "Sleep", ...
QString actionName(ActionType action);
// "Shut down", "Sleep", ... used in "Shut down in 60 s".
QString actionVerb(ActionType action);
// A display name for a device that has none.
QString deviceName(const DeviceConfig& device);
// The name of a setting on a device: "AC output", "Charge limit".
QString controlName(const QString& id);
// A setting's value for display: "On", "80 %", "1,800 W", "8 A", "2 h",
// "Never".
QString controlValue(const DeviceControl& control, double value);

// An Event stamped with time, device and status snapshot; title and message
// are left to the caller.
Event makeEvent(EventType type, const DeviceConfig* device, const DeviceStatus* status, const QDateTime& now);

}  // namespace nutty::text
