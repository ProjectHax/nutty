// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "app/viewmodels/EpisodeModel.h"

#include "app/viewmodels/AppContext.h"
#include "app/viewmodels/EventLogModel.h"
#include "core/engine/Monitor.h"

#include <algorithm>

namespace nutty {

EpisodeModel::EpisodeModel(QObject* parent)
    : EpisodeModel(AppContext::instance(), parent)
{
}

EpisodeModel::EpisodeModel(AppContext* context, QObject* parent)
    : QAbstractListModel(parent)
    , m_context(context)
{
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(0);
    connect(&m_reloadTimer, &QTimer::timeout, this, &EpisodeModel::reload);
    m_ongoingTimer.setInterval(15 * 1000);
    connect(&m_ongoingTimer, &QTimer::timeout, this, [this] {
        for (int row = 0; row < count(); ++row) {
            if (m_rows.at(row).ongoing())
                emit dataChanged(index(row), index(row), { DurationRole });
        }
        emit countChanged();  // totalSeconds
    });
    if (m_context && m_context->history())
        connect(m_context->history(), &HistoryStore::episodeChanged, this, &EpisodeModel::onEpisodeChanged);
    m_reloadTimer.start();
}

EpisodeModel::~EpisodeModel() = default;

QDateTime EpisodeModel::now() const
{
    return m_context ? m_context->monitor()->now() : QDateTime::currentDateTimeUtc();
}

int EpisodeModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant EpisodeModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= count())
        return {};
    const Episode& e = m_rows.at(index.row());
    switch (role) {
    case IdRole:
        return e.id;
    case DeviceIdRole:
        return idString(e.deviceId);
    case DeviceNameRole:
        return e.deviceName;
    case StartRole:
        return e.start;
    case EndRole:
        return e.end.isValid() ? QVariant(e.end) : QVariant();
    case OngoingRole:
        return e.ongoing();
    case DurationRole:
        return static_cast<double>(e.durationSec(now()));
    case StartChargeRole:
        return e.startCharge ? QVariant(*e.startCharge) : QVariant(-1.0);
    case MinChargeRole:
        return e.minCharge ? QVariant(*e.minCharge) : QVariant(-1.0);
    case MinRuntimeRole:
        return e.minRuntime ? QVariant(static_cast<double>(*e.minRuntime)) : QVariant(-1.0);
    case ActionsRole: {
        QVariantList list;
        for (const EpisodeAction& a : e.actions)
            list.append(QVariantMap {
                { QStringLiteral("timestamp"), a.timestamp }, { QStringLiteral("text"), a.text } });
        return list;
    }
    case DayRole:
        return EventLogModel::dayLabel(e.start, QDate::currentDate());
    default:
        return {};
    }
}

QHash<int, QByteArray> EpisodeModel::roleNames() const
{
    return {
        { IdRole, "episodeId" },
        { DeviceIdRole, "deviceId" },
        { DeviceNameRole, "deviceName" },
        { StartRole, "start" },
        { EndRole, "end" },
        { OngoingRole, "ongoing" },
        { DurationRole, "duration" },
        { StartChargeRole, "startCharge" },
        { MinChargeRole, "minCharge" },
        { MinRuntimeRole, "minRuntime" },
        { ActionsRole, "actions" },
        { DayRole, "day" },
    };
}

QVariantMap EpisodeModel::get(int row) const
{
    QVariantMap map;
    if (row < 0 || row >= count())
        return map;
    const auto names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        map.insert(QString::fromLatin1(it.value()), data(index(row), it.key()));
    return map;
}

void EpisodeModel::setDeviceId(const QString& id)
{
    if (id == m_deviceId)
        return;
    m_deviceId = id;
    emit filterChanged();
    m_reloadTimer.start();
}

void EpisodeModel::setFrom(const QDateTime& from)
{
    if (from == m_from)
        return;
    m_from = from;
    emit filterChanged();
    m_reloadTimer.start();
}

void EpisodeModel::setTo(const QDateTime& to)
{
    if (to == m_to)
        return;
    m_to = to;
    emit filterChanged();
    m_reloadTimer.start();
}

void EpisodeModel::setPageSize(int size)
{
    size = qBound(1, size, 1000);
    if (size == m_pageSize)
        return;
    m_pageSize = size;
    emit pageSizeChanged();
}

double EpisodeModel::totalSeconds() const
{
    const QDateTime t = now();
    double sum = 0;
    for (const Episode& e : m_rows)
        sum += static_cast<double>(e.durationSec(t));
    return sum;
}

void EpisodeModel::reload()
{
    m_reloadTimer.stop();
    HistoryStore* store = m_context ? m_context->history() : nullptr;
    beginResetModel();
    m_rows.clear();
    m_total = 0;
    if (store && store->isOpen()) {
        const QUuid device = parseId(m_deviceId);
        m_rows = store->episodes(device, m_from, m_to, m_pageSize, 0);
        m_total = store->countEpisodes(device, m_from, m_to);
    }
    endResetModel();
    const bool ongoing = std::ranges::any_of(m_rows, &Episode::ongoing);
    if (ongoing)
        m_ongoingTimer.start();
    else
        m_ongoingTimer.stop();
    emit countChanged();
}

void EpisodeModel::loadMore()
{
    HistoryStore* store = m_context ? m_context->history() : nullptr;
    if (!store || !hasMore())
        return;
    const auto more = store->episodes(parseId(m_deviceId), m_from, m_to, m_pageSize, count());
    if (more.isEmpty()) {
        m_total = count();
        emit countChanged();
        return;
    }
    beginInsertRows({}, count(), count() + static_cast<int>(more.size()) - 1);
    m_rows.append(more);
    endInsertRows();
    emit countChanged();
}

void EpisodeModel::onEpisodeChanged(qint64 id)
{
    HistoryStore* store = m_context ? m_context->history() : nullptr;
    if (!store)
        return;
    for (int row = 0; row < count(); ++row) {
        if (m_rows.at(row).id != id)
            continue;
        if (const auto updated = store->episode(id)) {
            m_rows[row] = *updated;
            emit dataChanged(index(row), index(row));
            emit countChanged();
        }
        return;
    }
    // A new outage (or one now in range): requery.
    m_reloadTimer.start();
}

bool EpisodeModel::exportCsv(const QUrl& file) const
{
    HistoryStore* store = m_context ? m_context->history() : nullptr;
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (!store || path.isEmpty())
        return false;
    return store->exportEpisodesCsv(path, parseId(m_deviceId), m_from, m_to);
}

}  // namespace nutty
