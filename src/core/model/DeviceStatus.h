// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include <QDateTime>
#include <QFlags>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

namespace nutty {

// Normalized status flags. The names follow NUT's ups.status tokens; every
// source maps its own states onto these.
enum class StatusFlag : quint32 {
    None = 0,
    Online = 1u << 0,  // OL
    OnBattery = 1u << 1,  // OB
    LowBattery = 1u << 2,  // LB
    HighBattery = 1u << 3,  // HB
    ReplaceBattery = 1u << 4,  // RB
    Charging = 1u << 5,  // CHRG
    Discharging = 1u << 6,  // DISCHRG
    Bypass = 1u << 7,  // BYPASS
    Calibrating = 1u << 8,  // CAL
    Off = 1u << 9,  // OFF
    Overload = 1u << 10,  // OVER
    Trim = 1u << 11,  // TRIM
    Boost = 1u << 12,  // BOOST
    ForcedShutdown = 1u << 13,  // FSD
    Alarm = 1u << 14,  // ALARM
};
using StatusFlags = QFlags<StatusFlag>;

// Parses a NUT-style status string ("OL CHRG", "OB LB") into flags. Unknown
// tokens are ignored.
StatusFlags parseStatusTokens(QStringView tokens);
// The inverse: "OL CHRG". Used for logs, notifications and the history DB.
QString statusTokens(StatusFlags flags);

// One poll's worth of normalized data from a source. Every measurement is
// optional because sources differ in what they report.
struct DeviceStatus {
    QDateTime sampledAt;  // wall clock (UTC) when this sample was taken
    StatusFlags flags;

    std::optional<double> charge;  // percent, 0..100
    std::optional<qint64> runtime;  // seconds remaining
    std::optional<double> load;  // percent of capacity
    std::optional<double> inputVoltage;  // V
    std::optional<double> outputVoltage;  // V
    std::optional<double> batteryVoltage;  // V
    std::optional<double> wattsIn;  // W flowing in (charging / input)
    std::optional<double> wattsOut;  // W flowing out (load)
    std::optional<double> wattsSolar;  // W of wattsIn from solar panels
    std::optional<double> temperature;  // °C
    std::optional<double> chargeLow;  // device's own low-battery threshold, percent
    std::optional<qint64> runtimeLow;  // device's own low-runtime threshold, seconds

    QString manufacturer;
    QString model;
    QString serial;
    QString alarmText;  // e.g. NUT ups.alarm

    // Everything the source reported, verbatim (NUT variables, apcupsd keys,
    // EcoFlow quota keys, ...). Shown on the device details page.
    QMap<QString, QString> raw;

    bool isValid() const { return sampledAt.isValid(); }
    bool has(StatusFlag flag) const { return flags.testFlag(flag); }
    bool onBattery() const { return has(StatusFlag::OnBattery); }
    bool online() const { return has(StatusFlag::Online); }
};

}  // namespace nutty

Q_DECLARE_OPERATORS_FOR_FLAGS(nutty::StatusFlags)
