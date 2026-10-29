// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/model/DeviceStatus.h"

#include <array>
#include <utility>

namespace nutty {

namespace {

constexpr std::array<std::pair<const char*, StatusFlag>, 15> kTokens { {
    { "OL", StatusFlag::Online },
    { "OB", StatusFlag::OnBattery },
    { "LB", StatusFlag::LowBattery },
    { "HB", StatusFlag::HighBattery },
    { "RB", StatusFlag::ReplaceBattery },
    { "CHRG", StatusFlag::Charging },
    { "DISCHRG", StatusFlag::Discharging },
    { "BYPASS", StatusFlag::Bypass },
    { "CAL", StatusFlag::Calibrating },
    { "OFF", StatusFlag::Off },
    { "OVER", StatusFlag::Overload },
    { "TRIM", StatusFlag::Trim },
    { "BOOST", StatusFlag::Boost },
    { "FSD", StatusFlag::ForcedShutdown },
    { "ALARM", StatusFlag::Alarm },
} };

}  // namespace

StatusFlags parseStatusTokens(QStringView tokens)
{
    StatusFlags flags;
    for (const auto token : tokens.split(u' ', Qt::SkipEmptyParts)) {
        for (const auto& [name, flag] : kTokens) {
            if (token.compare(QLatin1StringView(name), Qt::CaseInsensitive) == 0) {
                flags |= flag;
                break;
            }
        }
    }
    return flags;
}

QString statusTokens(StatusFlags flags)
{
    QStringList out;
    for (const auto& [name, flag] : kTokens) {
        if (flags.testFlag(flag))
            out << QString::fromLatin1(name);
    }
    return out.join(u' ');
}

}  // namespace nutty
