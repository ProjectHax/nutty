// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QHash>
#include <QPointer>
#include <QTimer>
#include <QUuid>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

namespace nutty {

class AppContext;

// Every configured device with its live state, as the singleton `Devices`.
// The roles match DeviceCard's properties (unknown numbers are -1), plus a
// 24-hour charge history for its sparkline.
class DeviceListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Devices)
    QML_SINGLETON
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int enabledCount READ enabledCount NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        SubtitleRole,
        TypeRole,
        TypeNameRole,
        KindRole,
        ImageSourceRole,
        EnabledRole,
        ChargeRole,
        RuntimeRole,
        LoadRole,
        WattsInRole,
        WattsOutRole,
        InputVoltageRole,
        OutputVoltageRole,
        TemperatureRole,
        StatusRole,
        StatusTextRole,
        ChargingRole,
        StaleRole,
        OfflineRole,
        HealthRole,
        LastUpdatedRole,
        HistoryRole,
        ErrorRole,
        EndpointRole,
        ModelRole,
        SerialRole,
        OnBatterySinceRole,
        WattsSolarRole,
    };
    Q_ENUM(Role)

    explicit DeviceListModel(AppContext* context = nullptr, QObject* parent = nullptr);
    ~DeviceListModel() override;
    static DeviceListModel* create(QQmlEngine*, QJSEngine*);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(m_ids.size()); }
    int enabledCount() const;

    // Every role of one device as a map (by role name); empty when unknown.
    Q_INVOKABLE QVariantMap get(const QString& id) const;
    Q_INVOKABLE int indexOf(const QString& id) const;
    // Everything the source reported, sorted by name: [{ key, value }].
    Q_INVOKABLE QVariantList rawValues(const QString& id) const;
    // Polls one device (or every device for an empty id) right away.
    Q_INVOKABLE void refresh(const QString& id = {});

signals:
    void countChanged();

private:
    void rebuild();
    void updateDevice(const QUuid& id);
    QVariantList history(const QUuid& id) const;
    void refreshHistory(const QUuid& id, bool force = false);

    QPointer<AppContext> m_context;
    QList<QUuid> m_ids;
    struct HistoryCache {
        QVariantList values;
        QDateTime fetchedAt;
    };
    QHash<QUuid, HistoryCache> m_history;
    QTimer m_historyTimer;
};

}  // namespace nutty
