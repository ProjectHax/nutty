// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Event.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

namespace nutty {

class AppContext;
struct EventQuery;

// The event log from the history database, newest first, a page at a time.
// New events matching the filter appear at the top as they are recorded.
//
//   EventLogModel { deviceId: "..."; category: "power"; limit: 8 }
//
// `limit` > 0 keeps at most that many rows (no paging), for summaries;
// otherwise loadMore() fetches the next `pageSize` rows.
class EventLogModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString deviceId READ deviceId WRITE setDeviceId NOTIFY filterChanged)
    // all, power, battery, connection, actions
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY filterChanged)
    // Event type keys ("OnBattery", ...); combined with category when both are set.
    Q_PROPERTY(QStringList types READ types WRITE setTypes NOTIFY filterChanged)
    Q_PROPERTY(QDateTime from READ from WRITE setFrom NOTIFY filterChanged)
    Q_PROPERTY(QDateTime to READ to WRITE setTo NOTIFY filterChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY filterChanged)
    Q_PROPERTY(int pageSize READ pageSize WRITE setPageSize NOTIFY pageSizeChanged)
    Q_PROPERTY(int limit READ limit WRITE setLimit NOTIFY limitChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int total READ total NOTIFY countChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        TimestampRole,
        TypeRole,
        SeverityRole,
        DeviceIdRole,
        DeviceNameRole,
        TitleRole,
        MessageRole,
        DayRole,
        ChargeRole,
        RuntimeRole,
    };
    Q_ENUM(Role)

    explicit EventLogModel(QObject* parent = nullptr);
    EventLogModel(AppContext* context, QObject* parent);
    ~EventLogModel() override;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString deviceId() const { return m_deviceId; }
    void setDeviceId(const QString& id);
    QString category() const { return m_category; }
    void setCategory(const QString& category);
    QStringList types() const { return m_types; }
    void setTypes(const QStringList& types);
    QDateTime from() const { return m_from; }
    void setFrom(const QDateTime& from);
    QDateTime to() const { return m_to; }
    void setTo(const QDateTime& to);
    QString search() const { return m_search; }
    void setSearch(const QString& search);
    int pageSize() const { return m_pageSize; }
    void setPageSize(int size);
    int limit() const { return m_limit; }
    void setLimit(int limit);

    int count() const { return static_cast<int>(m_rows.size()); }
    int total() const { return m_total; }
    bool hasMore() const { return m_limit <= 0 && count() < m_total; }

    Q_INVOKABLE void reload();
    Q_INVOKABLE void loadMore();
    // Writes every matching event (not only the loaded page) as CSV.
    Q_INVOKABLE bool exportCsv(const QUrl& file) const;
    Q_INVOKABLE QVariantMap get(int row) const;

    // The section label for a timestamp: "Today", "Yesterday" or a date.
    static QString dayLabel(const QDateTime& when, const QDate& today);
    // The event types behind a category key.
    static QSet<EventType> categoryTypes(const QString& category);

signals:
    void filterChanged();
    void pageSizeChanged();
    void limitChanged();
    void countChanged();

private:
    EventQuery query(int limit, int offset) const;
    bool matches(const Event& event) const;
    void onEventAdded(const Event& event);
    void scheduleReload();

    QPointer<AppContext> m_context;
    QString m_deviceId;
    QString m_category = QStringLiteral("all");
    QStringList m_types;
    QDateTime m_from;
    QDateTime m_to;
    QString m_search;
    int m_pageSize = 50;
    int m_limit = 0;
    int m_total = 0;
    QList<Event> m_rows;
    QTimer m_reloadTimer;
    QDate m_today;
};

}  // namespace nutty
