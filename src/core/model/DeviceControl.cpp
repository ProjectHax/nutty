// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/model/DeviceControl.h"

#include <algorithm>
#include <cmath>

namespace nutty {

bool DeviceControl::accepts(double v) const
{
    if (!std::isfinite(v))
        return false;
    switch (kind) {
    case ControlKind::Toggle:
        return v == 0 || v == 1;
    case ControlKind::Choice:
        return std::ranges::find(choices, v) != choices.end();
    case ControlKind::Level: {
        if (v < minimum || v > maximum)
            return false;
        if (step <= 0)
            return true;
        const double steps = (v - minimum) / step;
        return std::abs(steps - std::round(steps)) < 1e-6;
    }
    }
    return false;
}

bool DeviceControl::asksTwice(double v) const
{
    switch (ask) {
    case ControlAsk::Once:
        return false;
    case ControlAsk::Always:
        return true;
    case ControlAsk::WhenOff:
        return v == 0;
    case ControlAsk::UnlessZero:
        return v != 0;
    }
    return true;
}

}  // namespace nutty
