// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include <QHashFunctions>
#include <QMetaEnum>
#include <QObject>
#include <QString>
#include <QStringView>

// Shared enums. Their key names (via QMetaEnum) are the stable identifiers used
// in the config file and the history database, so never rename an enumerator;
// add new ones at the end.
namespace nutty {
Q_NAMESPACE

enum class SourceType {
    Nut,  // Network UPS Tools upsd (TCP 3493)
    Apcupsd,  // apcupsd NIS (TCP 3551)
    EcoFlow,  // EcoFlow IoT Open Platform (HTTPS REST)
    HttpJson,  // generic HTTP/JSON endpoint (Home Assistant preset)
    System,  // the OS's own power sources (UPower / IOKit / Win32)
};
Q_ENUM_NS(SourceType)

// What a device looks like on screen. Auto picks one from the source/model.
enum class DeviceKind {
    Auto,
    TowerUps,
    RackUps,
    PowerStation,
    HomeBattery,
    Laptop,
    Generic,
};
Q_ENUM_NS(DeviceKind)

enum class ConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Stale,  // connected, but the data hasn't refreshed in time
    Error,
};
Q_ENUM_NS(ConnectionState)

enum class SourceError {
    None,
    Network,  // connect/read failure, timeout, DNS
    Auth,  // bad credentials / access denied
    Protocol,  // unexpected or malformed response
    NotFound,  // unknown UPS / device serial / entity
    DataStale,  // the server says its data is stale (driver not connected)
    Config,  // incomplete or invalid settings
    Tls,  // TLS negotiation or certificate pinning failed
};
Q_ENUM_NS(SourceError)

enum class EventType {
    OnBattery,
    PowerRestored,
    LowBattery,
    ChargeBelow,
    RuntimeBelow,
    ForcedShutdown,
    ReplaceBattery,
    Overload,
    Bypass,
    UpsOff,
    Alarm,
    CommLost,
    CommRestored,
    AuthFailed,
    ActionScheduled,
    ActionCancelled,
    ActionExecuted,
    ActionFailed,
    Test,  // a test notification sent from the settings
    SettingChanged,  // the user changed a setting on the device itself
    SettingFailed,  // ...and the device didn't take it
};
Q_ENUM_NS(EventType)

enum class Severity {
    Info,
    Warning,
    Critical,
};
Q_ENUM_NS(Severity)

// Ordered by strength: when two rules fire, the larger value wins.
enum class ActionType {
    Notify,
    Command,
    Sleep,
    Hibernate,
    Shutdown,
};
Q_ENUM_NS(ActionType)

enum class ConditionType {
    OnBatteryFor,  // threshold: seconds on battery
    ChargeBelow,  // threshold: percent
    RuntimeBelow,  // threshold: seconds
    LowBattery,  // the device's own LB flag (latched until on line)
    ForcedShutdown,  // FSD from the NUT primary
    CommLostOnBattery,  // comms lost while the last known state was on battery
};
Q_ENUM_NS(ConditionType)

enum class RuleScope {
    Device,  // target is a device id
    GroupAny,  // target is a group id; fires when any member matches
    GroupAll,  // target is a group id; fires when every member matches
};
Q_ENUM_NS(RuleScope)

enum class ChannelType {
    Desktop,
    Pushover,
    Ntfy,
    Email,
    Webhook,
    PagerDuty,
};
Q_ENUM_NS(ChannelType)

enum class ThemeMode {
    System,
    Light,
    Dark,
};
Q_ENUM_NS(ThemeMode)

enum class MotionMode {
    System,
    On,
    Off,
};
Q_ENUM_NS(MotionMode)

enum class TrayIconStyle {
    Acorn,
    Battery,
};
Q_ENUM_NS(TrayIconStyle)

// How a setting on the device itself is changed (see DeviceControl).
enum class ControlKind {
    Toggle,  // on or off
    Level,  // a number in a range, in steps
    Choice,  // one of a few values
};
Q_ENUM_NS(ControlKind)

// When the app asks a second time before changing a setting, because the
// change can cut power to what's plugged in.
enum class ControlAsk {
    Once,
    Always,
    WhenOff,  // turning it off
    UnlessZero,  // any value but 0 (timeouts: 0 is "never")
};
Q_ENUM_NS(ControlAsk)

enum class ControlResult {
    Applied,  // the device reports the new value
    Unconfirmed,  // the cloud took it, but the device still reports the old value
    Failed,
};
Q_ENUM_NS(ControlResult)

// Hashing for QSet/QHash keys (Qt 6.4 has no generic enum qHash).
inline size_t qHash(EventType e, size_t seed = 0) noexcept
{
    return ::qHash(static_cast<int>(e), seed);
}
inline size_t qHash(SourceType e, size_t seed = 0) noexcept
{
    return ::qHash(static_cast<int>(e), seed);
}
inline size_t qHash(ChannelType e, size_t seed = 0) noexcept
{
    return ::qHash(static_cast<int>(e), seed);
}
inline size_t qHash(ActionType e, size_t seed = 0) noexcept
{
    return ::qHash(static_cast<int>(e), seed);
}

// Stable string key for an enum value, e.g. enumKey(SourceType::Nut) == "Nut".
template <typename E> QString enumKey(E value)
{
    return QString::fromLatin1(QMetaEnum::fromType<E>().valueToKey(static_cast<int>(value)));
}

// Inverse of enumKey(); returns fallback for unknown keys.
template <typename E> E enumFromKey(QStringView key, E fallback)
{
    bool ok = false;
    const int value = QMetaEnum::fromType<E>().keyToValue(key.toLatin1().constData(), &ok);
    return ok ? static_cast<E>(value) : fallback;
}

}  // namespace nutty
