// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include <QList>
#include <QString>

#include <optional>

namespace nutty {

// Readings beyond DeviceStatus that a source can break out for the device
// page: where the power comes from and goes, each battery pack, lifetime
// energy and temperatures. Ids name each item; the app turns them into labels
// and icons.
struct DeviceDetails {
    struct Reading {
        QString id;
        double value = 0;
    };
    struct Pack {
        QString id;  // main, extra1, extra2
        std::optional<double> charge;  // %
        std::optional<double> health;  // %
        std::optional<int> cycles;
        std::optional<double> temperature;  // °C
        std::optional<double> voltage;  // V
        std::optional<double> current;  // A, negative while discharging
        std::optional<double> fullCapacity;  // Ah
        std::optional<double> designCapacity;  // Ah
        std::optional<double> remainingCapacity;  // Ah
        std::optional<double> cellMin;  // V
        std::optional<double> cellMax;  // V
    };

    QList<Reading> inputs;  // W by input: ac, solar
    QList<Reading> outputs;  // W by output: ac, usb, usbc, car, wireless
    QList<Pack> packs;
    QList<Reading> energy;  // lifetime Wh: acIn, solarIn, dcIn, acOut, dcOut
    QList<Reading> temperatures;  // °C: inverter, solar, dcInput
    std::optional<qint64> chargeMinutes;  // until full, while charging

    bool isEmpty() const
    {
        return inputs.isEmpty() && outputs.isEmpty() && packs.isEmpty() && energy.isEmpty()
            && temperatures.isEmpty() && !chargeMinutes;
    }
};

}  // namespace nutty
