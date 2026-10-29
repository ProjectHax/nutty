// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "app/viewmodels/DeviceListModel.h"

#include "app/viewmodels/AppContext.h"
#include "app/viewmodels/DeviceInfo.h"
#include "core/engine/EventText.h"
#include "core/engine/Monitor.h"
#include "core/storage/HistoryStore.h"

#include <algorithm>

namespace nutty {

namespace {
// The sparkline covers a day in half-hour buckets.
constexpr int kHistoryHours = 24;
constexpr int kHistoryPoints = 48;
// Refetch a device's history at most this often while data streams in.
constexpr int kHistoryRefreshSec = 120;

QVariant number(const std::optional<double>& v)
{
    return v ? QVariant(*v) : QVariant(-1.0);
}
QVariant number(const std::optional<qint64>& v)
{
    return v ? QVariant(static_cast<double>(*v)) : QVariant(-1.0);
}
}  // namespace

DeviceListModel::DeviceListModel(AppContext* context, QObject* parent)
    : QAbstractListModel(parent)
    , m_context(context ? context : AppContext::instance())
{
    m_historyTimer.setInterval(5 * 60 * 1000);
    connect(&m_historyTimer, &QTimer::timeout, this, [this] {
        for (const QUuid& id : std::as_const(m_ids))
            refreshHistory(id, true);
    });
    if (!m_context)
        return;
    Monitor* monitor = m_context->monitor();
    connect(monitor, &Monitor::devicesChanged, this, &DeviceListModel::rebuild);
    connect(monitor, &Monitor::deviceStatusChanged, this, [this](const QUuid& id) {
        refreshHistory(id);
        updateDevice(id);
    });
    connect(monitor, &Monitor::deviceConnectionChanged, this, &DeviceListModel::updateDevice);
    // Names, images and kinds live in the config.
    connect(m_context, &AppContext::configChanged, this, [this] {
        for (const QUuid& id : std::as_const(m_ids))
            updateDevice(id);
    });
    m_historyTimer.start();
    rebuild();
}

DeviceListModel::~DeviceListModel() = default;

DeviceListModel* DeviceListModel::create(QQmlEngine*, QJSEngine*)
{
    return new DeviceListModel(AppContext::instance());
}

int DeviceListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : count();
}

int DeviceListModel::enabledCount() const
{
    if (!m_context)
        return 0;
    return static_cast<int>(std::ranges::count_if(m_ids, [this](const QUuid& id) {
        const DeviceConfig* d = m_context->monitor()->device(id);
        return d && d->enabled;
    }));
}

QVariant DeviceListModel::data(const QModelIndex& index, int role) const
{
    if (!m_context || !index.isValid() || index.row() >= count())
        return {};
    const QUuid id = m_ids.at(index.row());
    const Monitor& monitor = *m_context->monitor();
    const DeviceConfig* device = monitor.device(id);
    if (!device)
        return {};
    const DeviceStatus status = monitor.status(id);
    const QString key = deviceinfo::statusKey(monitor, id);

    switch (role) {
    case IdRole:
        return idString(id);
    case NameRole:
        return text::deviceName(*device);
    case SubtitleRole:
        return deviceinfo::subtitle(monitor, *device);
    case TypeRole:
        return enumKey(device->type);
    case TypeNameRole:
        return deviceinfo::typeName(device->type);
    case KindRole:
        return deviceinfo::kindKey(*device, status);
    case ImageSourceRole:
        return m_context->imageUrl(device->customImage);
    case EnabledRole:
        return device->enabled;
    case ChargeRole:
        return number(status.charge);
    case RuntimeRole:
        return number(deviceinfo::runtime(monitor, id));
    case LoadRole:
        return number(status.load);
    case WattsInRole:
        return number(status.wattsIn);
    case WattsSolarRole:
        return number(status.wattsSolar);
    case WattsOutRole:
        return number(status.wattsOut);
    case InputVoltageRole:
        return number(status.inputVoltage);
    case OutputVoltageRole:
        return number(status.outputVoltage);
    case TemperatureRole:
        return status.temperature ? QVariant(*status.temperature) : QVariant();
    case StatusRole:
        return key;
    case StatusTextRole:
        return deviceinfo::statusText(monitor, id);
    case ChargingRole:
        return key == u"charging";
    case StaleRole:
        return key == u"stale";
    case OfflineRole:
        return key == u"offline" || key == u"error" || !device->enabled;
    case HealthRole:
        return deviceinfo::healthKey(monitor.health(id));
    case LastUpdatedRole: {
        const QDateTime at = monitor.lastUpdate(id);
        return at.isValid() ? QVariant(at) : QVariant();
    }
    case HistoryRole:
        return history(id);
    case ErrorRole:
        return monitor.lastError(id);
    case EndpointRole:
        if (const Source* source = monitor.source(id))
            return source->describe();
        return QString();
    case ModelRole:
        return (status.manufacturer + u' ' + status.model).trimmed();
    case SerialRole:
        return status.serial;
    case OnBatterySinceRole: {
        const auto since = monitor.episodeStart(id);
        return since ? QVariant(*since) : QVariant();
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> DeviceListModel::roleNames() const
{
    return {
        { IdRole, "deviceId" },
        { NameRole, "name" },
        { SubtitleRole, "subtitle" },
        { TypeRole, "type" },
        { TypeNameRole, "typeName" },
        { KindRole, "kind" },
        { ImageSourceRole, "imageSource" },
        { EnabledRole, "enabled" },
        { ChargeRole, "charge" },
        { RuntimeRole, "runtime" },
        { LoadRole, "load" },
        { WattsInRole, "wattsIn" },
        { WattsSolarRole, "wattsSolar" },
        { WattsOutRole, "wattsOut" },
        { InputVoltageRole, "inputVoltage" },
        { OutputVoltageRole, "outputVoltage" },
        { TemperatureRole, "temperature" },
        { StatusRole, "status" },
        { StatusTextRole, "statusText" },
        { ChargingRole, "charging" },
        { StaleRole, "stale" },
        { OfflineRole, "offline" },
        { HealthRole, "health" },
        { LastUpdatedRole, "lastUpdated" },
        { HistoryRole, "history" },
        { ErrorRole, "error" },
        { EndpointRole, "endpoint" },
        { ModelRole, "modelName" },
        { SerialRole, "serial" },
        { OnBatterySinceRole, "onBatterySince" },
    };
}

QVariantMap DeviceListModel::get(const QString& id) const
{
    QVariantMap map;
    const int row = indexOf(id);
    if (row < 0)
        return map;
    const QModelIndex index = this->index(row);
    const auto names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        map.insert(QString::fromLatin1(it.value()), data(index, it.key()));
    return map;
}

int DeviceListModel::indexOf(const QString& id) const
{
    return static_cast<int>(m_ids.indexOf(parseId(id)));
}

QVariantList DeviceListModel::rawValues(const QString& id) const
{
    QVariantList list;
    if (!m_context)
        return list;
    const DeviceStatus status = m_context->monitor()->status(parseId(id));
    for (auto it = status.raw.cbegin(); it != status.raw.cend(); ++it)
        list.append(
            QVariantMap { { QStringLiteral("key"), it.key() }, { QStringLiteral("value"), it.value() } });
    return list;
}

void DeviceListModel::refresh(const QString& id)
{
    if (!m_context)
        return;
    Monitor* monitor = m_context->monitor();
    if (id.isEmpty()) {
        monitor->pollAll();
        return;
    }
    if (Source* source = monitor->source(parseId(id)))
        source->pollNow();
}

void DeviceListModel::rebuild()
{
    const QList<QUuid> ids = m_context ? m_context->monitor()->deviceIds() : QList<QUuid>();
    if (ids == m_ids) {
        for (const QUuid& id : ids)
            updateDevice(id);
        return;
    }
    beginResetModel();
    m_ids = ids;
    for (auto it = m_history.begin(); it != m_history.end();) {
        if (!m_ids.contains(it.key()))
            it = m_history.erase(it);
        else
            ++it;
    }
    for (const QUuid& id : std::as_const(m_ids))
        refreshHistory(id);
    endResetModel();
    emit countChanged();
}

void DeviceListModel::updateDevice(const QUuid& id)
{
    const int row = static_cast<int>(m_ids.indexOf(id));
    if (row < 0)
        return;
    const QModelIndex index = this->index(row);
    emit dataChanged(index, index);
    emit countChanged();  // enabledCount may have changed
}

QVariantList DeviceListModel::history(const QUuid& id) const
{
    return m_history.value(id).values;
}

void DeviceListModel::refreshHistory(const QUuid& id, bool force)
{
    HistoryStore* store = m_context ? m_context->history() : nullptr;
    if (!store || !store->isOpen())
        return;
    const QDateTime now = m_context->monitor()->now();
    HistoryCache& cache = m_history[id];
    if (!force && cache.fetchedAt.isValid() && cache.fetchedAt.secsTo(now) < kHistoryRefreshSec)
        return;
    cache.fetchedAt = now;
    QVariantList values;
    const auto samples = store->samples(id, now.addSecs(-kHistoryHours * 3600), now, kHistoryPoints);
    for (const Sample& s : samples) {
        if (s.charge)
            values.append(*s.charge);
    }
    if (values != cache.values) {
        cache.values = values;
        const int row = static_cast<int>(m_ids.indexOf(id));
        if (row >= 0 && force)
            emit dataChanged(index(row), index(row), { HistoryRole });
    }
}

}  // namespace nutty
