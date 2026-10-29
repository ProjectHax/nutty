// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "app/viewmodels/EventLogModel.h"

#include "app/viewmodels/AppContext.h"
#include "core/storage/HistoryStore.h"

#include <QLocale>

namespace nutty {

EventLogModel::EventLogModel(QObject* parent)
    : EventLogModel(AppContext::instance(), parent)
{
}

EventLogModel::EventLogModel(AppContext* context, QObject* parent)
    : QAbstractListModel(parent)
    , m_context(context)
{
    // Filters often change several properties in a row (from QML bindings);
    // query once after they settle.
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(0);
    connect(&m_reloadTimer, &QTimer::timeout, this, &EventLogModel::reload);
    if (m_context && m_context->history())
        connect(m_context->history(), &HistoryStore::eventAdded, this, &EventLogModel::onEventAdded);
    scheduleReload();
}

EventLogModel::~EventLogModel() = default;

int EventLogModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : count();
}

QString EventLogModel::dayLabel(const QDateTime& when, const QDate& today)
{
    const QDate day = when.toLocalTime().date();
    if (day == today)
        return tr("Today");
    if (day == today.addDays(-1))
        return tr("Yesterday");
    const QLocale locale;
    if (day.year() == today.year())
        return locale.toString(day, tr("dddd, MMMM d", "date format for this year"));
    return locale.toString(day, QLocale::LongFormat);
}

QVariant EventLogModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= count())
        return {};
    const Event& e = m_rows.at(index.row());
    switch (role) {
    case IdRole:
        return e.id;
    case TimestampRole:
        return e.timestamp;
    case TypeRole:
        return enumKey(e.type);
    case SeverityRole:
        return enumKey(e.severity);
    case DeviceIdRole:
        return idString(e.deviceId);
    case DeviceNameRole:
        return e.deviceName;
    case TitleRole:
        return e.title;
    case MessageRole:
        return e.message;
    case DayRole:
        return dayLabel(e.timestamp, m_today.isValid() ? m_today : QDate::currentDate());
    case ChargeRole:
        return e.charge ? QVariant(*e.charge) : QVariant(-1.0);
    case RuntimeRole:
        return e.runtime ? QVariant(static_cast<double>(*e.runtime)) : QVariant(-1.0);
    default:
        return {};
    }
}

QHash<int, QByteArray> EventLogModel::roleNames() const
{
    return {
        { IdRole, "eventId" },
        { TimestampRole, "timestamp" },
        { TypeRole, "type" },
        { SeverityRole, "severity" },
        { DeviceIdRole, "deviceId" },
        { DeviceNameRole, "deviceName" },
        { TitleRole, "title" },
        { MessageRole, "message" },
        { DayRole, "day" },
        { ChargeRole, "charge" },
        { RuntimeRole, "runtime" },
    };
}

QVariantMap EventLogModel::get(int row) const
{
    QVariantMap map;
    if (row < 0 || row >= count())
        return map;
    const auto names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        map.insert(QString::fromLatin1(it.value()), data(index(row), it.key()));
    return map;
}

void EventLogModel::setDeviceId(const QString& id)
{
    if (id == m_deviceId)
        return;
    m_deviceId = id;
    emit filterChanged();
    scheduleReload();
}

void EventLogModel::setCategory(const QString& category)
{
    const QString value = category.isEmpty() ? QStringLiteral("all") : category;
    if (value == m_category)
        return;
    m_category = value;
    emit filterChanged();
    scheduleReload();
}

void EventLogModel::setTypes(const QStringList& types)
{
    if (types == m_types)
        return;
    m_types = types;
    emit filterChanged();
    scheduleReload();
}

void EventLogModel::setFrom(const QDateTime& from)
{
    if (from == m_from)
        return;
    m_from = from;
    emit filterChanged();
    scheduleReload();
}

void EventLogModel::setTo(const QDateTime& to)
{
    if (to == m_to)
        return;
    m_to = to;
    emit filterChanged();
    scheduleReload();
}

void EventLogModel::setSearch(const QString& search)
{
    if (search == m_search)
        return;
    m_search = search;
    emit filterChanged();
    scheduleReload();
}

void EventLogModel::setPageSize(int size)
{
    size = qBound(1, size, 1000);
    if (size == m_pageSize)
        return;
    m_pageSize = size;
    emit pageSizeChanged();
}

void EventLogModel::setLimit(int limit)
{
    limit = qMax(0, limit);
    if (limit == m_limit)
        return;
    m_limit = limit;
    emit limitChanged();
    scheduleReload();
}

QSet<EventType> EventLogModel::categoryTypes(const QString& category)
{
    if (category == u"power")
        return { EventType::OnBattery, EventType::PowerRestored, EventType::ForcedShutdown, EventType::Bypass,
            EventType::Overload, EventType::UpsOff };
    if (category == u"battery")
        return { EventType::LowBattery, EventType::ChargeBelow, EventType::RuntimeBelow,
            EventType::ReplaceBattery, EventType::Alarm };
    if (category == u"connection")
        return { EventType::CommLost, EventType::CommRestored, EventType::AuthFailed };
    if (category == u"actions")
        return { EventType::ActionScheduled, EventType::ActionCancelled, EventType::ActionExecuted,
            EventType::ActionFailed, EventType::Test, EventType::SettingChanged, EventType::SettingFailed };
    return {};
}

EventQuery EventLogModel::query(int limit, int offset) const
{
    EventQuery q;
    if (const QUuid device = parseId(m_deviceId); !device.isNull())
        q.devices = { device };
    q.types = categoryTypes(m_category);
    if (!m_types.isEmpty()) {
        QSet<EventType> explicitTypes;
        for (const QString& key : m_types)
            explicitTypes.insert(enumFromKey(key, EventType::Test));
        q.types = q.types.isEmpty() ? explicitTypes : q.types.intersect(explicitTypes);
        if (q.types.isEmpty())
            q.types = { EventType::Test };  // nothing can match both; keep it empty-ish
    }
    q.from = m_from;
    q.to = m_to;
    q.search = m_search;
    q.limit = limit;
    q.offset = offset;
    q.newestFirst = true;
    return q;
}

bool EventLogModel::matches(const Event& event) const
{
    const EventQuery q = query(1, 0);
    if (!q.devices.isEmpty() && !q.devices.contains(event.deviceId))
        return false;
    if (!q.types.isEmpty() && !q.types.contains(event.type))
        return false;
    if (q.from.isValid() && event.timestamp < q.from)
        return false;
    if (q.to.isValid() && event.timestamp > q.to)
        return false;
    if (!q.search.isEmpty() && !event.title.contains(q.search, Qt::CaseInsensitive)
        && !event.message.contains(q.search, Qt::CaseInsensitive))
        return false;
    return true;
}

void EventLogModel::scheduleReload()
{
    m_reloadTimer.start();
}

void EventLogModel::reload()
{
    m_reloadTimer.stop();
    HistoryStore* store = m_context ? m_context->history() : nullptr;
    beginResetModel();
    m_today = QDate::currentDate();
    m_rows.clear();
    m_total = 0;
    if (store && store->isOpen()) {
        const int first = m_limit > 0 ? m_limit : m_pageSize;
        m_rows = store->events(query(first, 0));
        m_total = store->countEvents(query(0, 0));
    }
    endResetModel();
    emit countChanged();
}

void EventLogModel::loadMore()
{
    HistoryStore* store = m_context ? m_context->history() : nullptr;
    if (!store || !hasMore())
        return;
    const QList<Event> more = store->events(query(m_pageSize, count()));
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

void EventLogModel::onEventAdded(const Event& event)
{
    if (m_reloadTimer.isActive() || !matches(event))
        return;
    // A new day means every "Today" label is stale.
    if (m_today != QDate::currentDate()) {
        scheduleReload();
        return;
    }
    beginInsertRows({}, 0, 0);
    m_rows.prepend(event);
    endInsertRows();
    ++m_total;
    if (m_limit > 0 && count() > m_limit) {
        beginRemoveRows({}, count() - 1, count() - 1);
        m_rows.removeLast();
        endRemoveRows();
    }
    emit countChanged();
}

bool EventLogModel::exportCsv(const QUrl& file) const
{
    HistoryStore* store = m_context ? m_context->history() : nullptr;
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (!store || path.isEmpty())
        return false;
    return store->exportEventsCsv(path, query(-1, 0));
}

}  // namespace nutty
