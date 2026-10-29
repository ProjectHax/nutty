// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/storage/HistoryStore.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

namespace nutty {

class AppContext;

// On-battery episodes ("outages") from the history database, newest first.
// Follows HistoryStore::episodeChanged, so an ongoing outage updates live.
class EpisodeModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString deviceId READ deviceId WRITE setDeviceId NOTIFY filterChanged)
    Q_PROPERTY(QDateTime from READ from WRITE setFrom NOTIFY filterChanged)
    Q_PROPERTY(QDateTime to READ to WRITE setTo NOTIFY filterChanged)
    Q_PROPERTY(int pageSize READ pageSize WRITE setPageSize NOTIFY pageSizeChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int total READ total NOTIFY countChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY countChanged)
    // Total seconds on battery across the matching episodes.
    Q_PROPERTY(double totalSeconds READ totalSeconds NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        DeviceIdRole,
        DeviceNameRole,
        StartRole,
        EndRole,
        OngoingRole,
        DurationRole,
        StartChargeRole,
        MinChargeRole,
        MinRuntimeRole,
        ActionsRole,
        DayRole,
    };
    Q_ENUM(Role)

    explicit EpisodeModel(QObject* parent = nullptr);
    EpisodeModel(AppContext* context, QObject* parent);
    ~EpisodeModel() override;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString deviceId() const { return m_deviceId; }
    void setDeviceId(const QString& id);
    QDateTime from() const { return m_from; }
    void setFrom(const QDateTime& from);
    QDateTime to() const { return m_to; }
    void setTo(const QDateTime& to);
    int pageSize() const { return m_pageSize; }
    void setPageSize(int size);

    int count() const { return static_cast<int>(m_rows.size()); }
    int total() const { return m_total; }
    bool hasMore() const { return count() < m_total; }
    double totalSeconds() const;

    Q_INVOKABLE void reload();
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE bool exportCsv(const QUrl& file) const;
    Q_INVOKABLE QVariantMap get(int row) const;

signals:
    void filterChanged();
    void pageSizeChanged();
    void countChanged();

private:
    void onEpisodeChanged(qint64 id);
    QDateTime now() const;

    QPointer<AppContext> m_context;
    QString m_deviceId;
    QDateTime m_from;
    QDateTime m_to;
    int m_pageSize = 50;
    int m_total = 0;
    QList<Episode> m_rows;
    QTimer m_reloadTimer;
    QTimer m_ongoingTimer;  // ticks the duration of ongoing outages
};

}  // namespace nutty
