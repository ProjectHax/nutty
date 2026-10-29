// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Types.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QUuid>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace nutty {

class AppContext;

// The settings of one device that nutty can change, and the readings its
// source breaks out, for the device page.
//
//   DeviceControls { id: settings; deviceId: page.deviceId }
//   settings.confirmation("acOutput", 0)  // what to ask first
//   settings.apply("acOutput", 0)         // once the user has said yes
//
// Nothing here asks the user; the page must, every time, before apply().
class DeviceControls : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString deviceId READ deviceId WRITE setDeviceId NOTIFY deviceIdChanged)
    // [{ id, group, kind ("Toggle", "Level", "Choice"), title, description,
    //    icon, known, value, valueText, minimum, maximum, step, unit,
    //    options: [{ value, text }], enabled, pending, pendingText,
    //    result ("", "Applied", "Unconfirmed", "Failed"), message }]
    Q_PROPERTY(QVariantList controls READ controls NOTIFY changed)
    // [{ key, title, controls: [indexes into controls] }] in display order.
    // It only changes when the list of settings does, so rows built from it
    // stay put while values update.
    Q_PROPERTY(QVariantList groups READ groups NOTIFY groupsChanged)
    Q_PROPERTY(bool available READ available NOTIFY groupsChanged)
    // Why no change can be sent right now; empty when one can.
    Q_PROPERTY(QString blockedReason READ blockedReason NOTIFY changed)
    // { inputs: [{ id, title, icon, watts }], outputs: [...],
    //   packs: [{ id, title, charge, health, cycles, temperature, voltage,
    //             current, watts, fullCapacity, designCapacity,
    //             remainingCapacity, cellMin, cellMax, storedWh }],
    //   energy: [{ id, title, icon, wh }], temperatures: [{ id, title, value }],
    //   chargeMinutes }; numbers that aren't reported are left out.
    Q_PROPERTY(QVariantMap details READ details NOTIFY changed)
    Q_PROPERTY(bool hasDetails READ hasDetails NOTIFY changed)

public:
    explicit DeviceControls(QObject* parent = nullptr);
    DeviceControls(AppContext* context, QObject* parent);

    QString deviceId() const { return m_deviceId; }
    void setDeviceId(const QString& id);
    QVariantList controls() const { return m_controls; }
    QVariantList groups() const { return m_groups; }
    bool available() const { return !m_groups.isEmpty(); }
    QString blockedReason() const;
    QVariantMap details() const { return m_details; }
    bool hasDetails() const { return m_hasDetails; }

    // The questions to ask before changing `id` to `value`:
    // { title, message, confirmText, twice, warningTitle, warningMessage,
    //   warningConfirmText }. `twice` means ask the warning as well.
    Q_INVOKABLE QVariantMap confirmation(const QString& id, double value) const;
    // Sends a change the user has confirmed. False when it can't be sent;
    // the reason shows as that control's message.
    Q_INVOKABLE bool apply(const QString& id, double value);

    // How long a "done" stays on a control after its change shows up.
    static constexpr int kAppliedShownMs = 5000;

signals:
    void deviceIdChanged();
    void changed();
    void groupsChanged();
    // A change finished: ok when the device reports the new value.
    void finished(const QString& id, bool ok, const QString& message);

private:
    struct Outcome {
        ControlResult result = ControlResult::Applied;
        QString message;
        int generation = 0;
    };

    void reload();
    void setOutcome(const QString& id, ControlResult result, const QString& message);
    QString deviceName() const;

    QPointer<AppContext> m_context;
    QString m_deviceId;
    QUuid m_id;
    QVariantList m_controls;
    QVariantList m_groups;
    QVariantMap m_details;
    bool m_hasDetails = false;
    QHash<QString, Outcome> m_outcomes;
    int m_generation = 0;
};

}  // namespace nutty
