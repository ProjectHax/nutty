// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/DeviceControl.h"
#include "core/model/DeviceDetails.h"
#include "core/model/DeviceStatus.h"

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <expected>
#include <optional>

namespace nutty::ecoflow {

// Turns an EcoFlow quota object into a DeviceStatus, its details and the
// settings nutty can change, driven by the JSON table in
// sources/resources/ecoflow-mapping.json (one entry per product family).
class EcoFlowMapping
{
public:
    static constexpr auto kResourcePath = ":/nutty/sources/resources/ecoflow-mapping.json";

    struct Result {
        DeviceStatus status;
        QString familyId;
        QString familyLabel;
        bool limited = false;  // the cloud API reports little for this family
        bool hasBattery = true;  // false for e.g. smart plugs
    };

    // The table compiled into the binary (loaded once).
    static const EcoFlowMapping& builtin();
    static EcoFlowMapping fromJson(const QByteArray& json);

    bool isValid() const { return !m_families.isEmpty(); }
    QStringList familyIds() const;

    Result map(const QJsonObject& quota) const;
    DeviceDetails details(const QJsonObject& quota) const;

    // The settings a device lets nutty change, with their current values.
    // Empty unless its family has a command table for its serial number.
    QList<DeviceControl> controls(const QJsonObject& quota, const QString& serial) const;
    // The "params" of the request that sets control `id` to `value`, or why
    // it can't be sent (unknown control, a value it doesn't take, a reading
    // the command needs is missing).
    std::expected<QJsonObject, QString> command(
        const QJsonObject& quota, const QString& serial, const QString& id, double value) const;

private:
    struct Candidate {
        QString key;
        double scale = 1.0;
    };
    struct Flow {
        QString id;
        QStringList keys;
    };
    struct Pack {
        QString id;
        QString prefix;
        QString online;
    };
    struct Reading {
        QString id;
        QString key;
    };
    struct Limit {
        QString key;
        double below = 0;
        double maximum = 0;
    };
    struct Control {
        DeviceControl control;  // without its value
        QString state;
        double on = 1;
        double off = 0;
        QList<Limit> limits;
        QJsonObject params;
    };
    struct Family {
        QString id;
        QString label;
        QStringList match;
        bool limited = false;
        bool battery = true;
        QHash<QString, QList<Candidate>> fields;
        QList<Flow> inputs;
        QList<Flow> outputs;
        QList<Pack> packs;
        QList<Reading> energy;
        QList<Reading> temperatures;
        QStringList controlSerials;
        QList<Control> controls;
    };

    static void parseDetails(Family& family, const QJsonObject& details);
    static void parseControls(Family& family, const QJsonObject& controls);
    const Family* detect(const QJsonObject& quota) const;
    const Family* controlFamily(const QJsonObject& quota, const QString& serial) const;
    static DeviceControl current(const Control& c, const QJsonObject& quota);
    static std::optional<double> field(const Family& family, const QJsonObject& quota, const QString& name);

    QList<Family> m_families;
};

// A remaining-time value in minutes, or nullopt when EcoFlow marks it as not
// available (< 0 or > 5000).
std::optional<qint64> validMinutes(std::optional<double> minutes);

}  // namespace nutty::ecoflow
