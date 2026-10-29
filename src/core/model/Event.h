// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/DeviceStatus.h"
#include "core/model/Types.h"

#include <QDateTime>
#include <QString>
#include <QUuid>
#include <QVariantMap>

#include <optional>

namespace nutty {

// Something that happened to a device (or to a power action). Events are what
// the history log stores and what the notification channels deliver.
struct Event {
    qint64 id = 0;  // row id once stored in the history DB
    QDateTime timestamp;  // UTC
    EventType type = EventType::Test;
    Severity severity = Severity::Info;
    QUuid deviceId;  // null for events that aren't about one device
    QString deviceName;
    QString title;  // short, translated, e.g. "On battery"
    QString message;  // one line, translated, e.g. "UPS-A is on battery: 87 %, 24 min left"

    // A snapshot of the device at that moment, for the log and for templates.
    StatusFlags flags;
    std::optional<double> charge;
    std::optional<qint64> runtime;
    std::optional<double> load;

    QVariantMap extra;  // event-specific details (rule name, action, error text, ...)
};

// The severity an event type gets unless the detector decides otherwise.
Severity defaultSeverity(EventType type);

}  // namespace nutty
