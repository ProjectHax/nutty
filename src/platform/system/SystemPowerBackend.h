// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include <optional>

namespace nutty {

// One power source as the OS reports it: a laptop battery or a UPS attached
// over USB (UPower on Linux, IOKit power sources on macOS, Win32 on Windows).
struct SystemPowerInfo {
    enum class Kind { Battery, Ups };
    enum class State { Unknown, Charging, Discharging, Full, NotCharging };

    QString id;  // stable identifier, stored in keys::SystemSourceId
    QString name;  // display name
    Kind kind = Kind::Battery;
    State state = State::Unknown;
    bool present = true;
    std::optional<bool> acOnline;  // the machine's mains state, if known

    std::optional<double> charge;  // percent
    std::optional<qint64> timeToEmpty;  // seconds
    std::optional<qint64> timeToFull;  // seconds
    std::optional<double> watts;  // energy rate, W (positive)
    std::optional<double> voltage;  // V
    std::optional<double> temperature;  // °C
    bool low = false;  // the OS's own low/critical warning
    bool critical = false;  // the OS is about to act on its own

    QString vendor;
    QString model;
    QString serial;
};

// Reads the OS's power sources. One implementation per OS, picked at build
// time by create(). Linux is asynchronous (D-Bus); the others answer in
// refresh() directly. Every implementation emits changed() after refresh()
// once new data is in sources(), and may emit it on its own when the OS
// reports a change.
class SystemPowerBackend : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    static SystemPowerBackend* create(QObject* parent = nullptr);

    // false when the OS service is missing (e.g. no UPower on the system bus).
    virtual bool isAvailable() const = 0;
    // true once the first answer (data or unavailability) has arrived.
    virtual bool isReady() const = 0;
    virtual QList<SystemPowerInfo> sources() const = 0;
    // Asks for fresh data; changed() follows.
    virtual void refresh() = 0;
    // Human-readable name of the OS interface, e.g. "UPower".
    virtual QString backendName() const = 0;

signals:
    void ready();
    void changed();
};

}  // namespace nutty
