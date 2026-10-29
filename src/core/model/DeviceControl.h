// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Types.h"

#include <QList>
#include <QString>

#include <optional>

namespace nutty {

// A setting on the device itself that nutty can change, like an EcoFlow's AC
// outlets or charge limit. The source describes each one with its current
// value; the app asks the user before applying a change (twice when `ask`
// says so) and then calls Source::applyControl(). Every value is a number:
// toggles are 0 or 1.
struct DeviceControl {
    QString id;  // acOutput, chargeLimit, ... (the app names them and picks their icons)
    ControlKind kind = ControlKind::Toggle;
    std::optional<double> value;  // what the device reports now; nullopt = not reported
    double minimum = 0;  // Level
    double maximum = 100;
    double step = 1;
    QList<double> choices;  // Choice
    QString unit;  // "%", "W", "mA", "s", "min"
    QString dependsOn;  // the id of a toggle that must be on for this one to apply
    ControlAsk ask = ControlAsk::Once;
    std::optional<double> pending;  // a change on its way to the device

    // Whether `v` is a value this control can be set to.
    bool accepts(double v) const;
    // Whether changing to `v` gets the second, sterner question.
    bool asksTwice(double v) const;
};

}  // namespace nutty
