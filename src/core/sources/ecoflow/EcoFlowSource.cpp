// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/sources/ecoflow/EcoFlowSource.h"

#include "core/sources/ecoflow/EcoFlowClient.h"
#include "core/sources/ecoflow/EcoFlowMapping.h"

#include <QJsonArray>
#include <QTimer>
#include <QUrl>

namespace nutty {

namespace {

const auto kQuotaPath = QStringLiteral("/iot-open/sign/device/quota/all");
const auto kSetPath = QStringLiteral("/iot-open/sign/device/quota");
const auto kDeviceListPath = QStringLiteral("/iot-open/sign/device/list");

bool isOnline(const QJsonValue& v)
{
    return v.isBool() ? v.toBool() : v.toInt() != 0;
}

}  // namespace

EcoFlowSource::EcoFlowSource(DeviceConfig config, QObject* parent)
    : Source(std::move(config), parent)
{
    m_client = new ecoflow::EcoFlowClient(this->config().settings, this->config().secrets, this);
    m_serial = this->config().settings.value(QLatin1StringView(keys::EcoSerial)).toString().trimmed();
    connect(&m_pollTimer, &QTimer::timeout, this, &EcoFlowSource::poll);
    m_confirmTimer.setInterval(kConfirmPollMs);
    connect(&m_confirmTimer, &QTimer::timeout, this, [this] {
        if (m_pending && m_pending->since.elapsed() >= m_confirmTimeoutMs)
            finishPending(ControlResult::Unconfirmed);
        else
            poll();
    });
}

EcoFlowSource::~EcoFlowSource()
{
    m_pollTimer.stop();
}

void EcoFlowSource::setConfirmTiming(int pollMs, int timeoutMs)
{
    m_confirmTimer.setInterval(pollMs);
    m_confirmTimeoutMs = timeoutMs;
}

QString EcoFlowSource::describe() const
{
    return QStringLiteral("%1@%2").arg(m_serial, QUrl(m_client->base()).host());
}

void EcoFlowSource::start()
{
    m_running = true;
    if (!m_client->hasCredentials()) {
        reportError(SourceError::Config, tr("EcoFlow access key and secret key are required"));
        return;
    }
    if (m_serial.isEmpty()) {
        reportError(SourceError::Config, tr("No EcoFlow device serial number is configured"));
        return;
    }
    setConnectionState(ConnectionState::Connecting);
    m_pollTimer.start(pollIntervalMs());
    refreshDeviceInfo();
    poll();
}

void EcoFlowSource::stop()
{
    m_running = false;
    m_pollTimer.stop();
    if (m_pending)
        finishPending(m_pending->sent ? ControlResult::Unconfirmed : ControlResult::Failed,
            tr("nutty stopped watching the device before it confirmed the change."));
    setConnectionState(ConnectionState::Disconnected);
}

void EcoFlowSource::pollNow()
{
    poll();
}

void EcoFlowSource::refreshDeviceInfo()
{
    if (m_infoInFlight)
        return;
    m_infoInFlight = true;
    m_client->get(kDeviceListPath, {}, [this](const ecoflow::EcoFlowClient::Result& result) {
        m_infoInFlight = false;
        m_infoFetchedAt = QDateTime::currentDateTimeUtc();
        if (!result)
            return;  // the quota poll reports errors; this is only extra info
        for (const QJsonValue& v : result->toArray()) {
            const QJsonObject o = v.toObject();
            if (o.value(u"sn").toString() != m_serial)
                continue;
            m_online = isOnline(o.value(u"online"));
            m_productName = o.value(u"productName").toString();
            break;
        }
    });
}

void EcoFlowSource::poll()
{
    if (!m_running || m_inFlight || !m_client->hasCredentials() || m_serial.isEmpty())
        return;
    if (!m_infoFetchedAt.isValid()
        || m_infoFetchedAt.secsTo(QDateTime::currentDateTimeUtc()) >= kDeviceInfoRefreshSec)
        refreshDeviceInfo();

    m_inFlight = true;
    m_client->get(kQuotaPath, QJsonObject { { QStringLiteral("sn"), m_serial } },
        [this](const ecoflow::EcoFlowClient::Result& result) {
            m_inFlight = false;
            if (!m_running)
                return;
            if (!result) {
                reportError(result.error().kind, result.error().message);
                return;
            }
            if (m_online && !*m_online) {
                reportError(SourceError::Network, tr("The EcoFlow device is offline"));
                return;
            }
            const QJsonObject quota = result->toObject();
            if (quota.isEmpty()) {
                reportError(SourceError::Protocol, tr("EcoFlow returned no data for this device"));
                return;
            }
            const auto& mapping = ecoflow::EcoFlowMapping::builtin();
            auto mapped = mapping.map(quota);
            mapped.status.serial = m_serial;
            mapped.status.model = !m_productName.isEmpty() ? m_productName : mapped.familyLabel;
            m_quota = quota;
            m_details = mapping.details(quota);
            if (!mapped.status.has(StatusFlag::Charging))
                m_details.chargeMinutes.reset();
            publish(std::move(mapped.status));
            emit controlsChanged();
            checkPending();
        });
}

// --- settings ---------------------------------------------------------------------

QList<DeviceControl> EcoFlowSource::controls() const
{
    QList<DeviceControl> list = ecoflow::EcoFlowMapping::builtin().controls(m_quota, m_serial);
    if (m_pending) {
        for (DeviceControl& c : list) {
            if (c.id == m_pending->id)
                c.pending = m_pending->value;
        }
    }
    return list;
}

bool EcoFlowSource::applyControl(const QString& id, double value, QString* error)
{
    const auto refuse = [&](const QString& why) {
        if (error)
            *error = why;
        return false;
    };
    if (!m_running || m_quota.isEmpty())
        return refuse(tr("nutty hasn't read the device yet."));
    if (m_online && !*m_online)
        return refuse(tr("The EcoFlow device is offline"));
    if (m_pending)
        return refuse(tr("Another change is still on its way to the device."));
    const auto params = ecoflow::EcoFlowMapping::builtin().command(m_quota, m_serial, id, value);
    if (!params)
        return refuse(params.error());

    m_pending = Pending { id, value, false, {} };
    m_pending->since.start();
    emit controlsChanged();
    const QJsonObject body { { QStringLiteral("sn"), m_serial }, { QStringLiteral("params"), *params } };
    m_client->put(kSetPath, body, [this](const ecoflow::EcoFlowClient::Result& result) {
        if (!m_pending)
            return;
        if (!result) {
            finishPending(ControlResult::Failed, result.error().message);
            return;
        }
        // EcoFlow passes the change on to the device; it shows in the
        // readings a few seconds later.
        m_pending->sent = true;
        m_pending->since.start();
        m_confirmTimer.start();
    });
    return true;
}

void EcoFlowSource::checkPending()
{
    if (!m_pending || !m_pending->sent)
        return;
    for (const DeviceControl& c : ecoflow::EcoFlowMapping::builtin().controls(m_quota, m_serial)) {
        if (c.id == m_pending->id && c.value && *c.value == m_pending->value) {
            finishPending(ControlResult::Applied);
            return;
        }
    }
}

void EcoFlowSource::finishPending(ControlResult result, const QString& error)
{
    if (!m_pending)
        return;
    const Pending done = *m_pending;
    m_pending.reset();
    m_confirmTimer.stop();
    emit controlsChanged();
    emit controlFinished(done.id, done.value, result, error);
}

// --- EcoFlowDiscovery ---------------------------------------------------------

EcoFlowDiscovery::EcoFlowDiscovery(const QVariantMap& settings, const QVariantMap& secrets, QObject* parent)
    : Discovery(parent)
{
    auto* client = new ecoflow::EcoFlowClient(settings, secrets, this);
    if (!client->hasCredentials()) {
        QTimer::singleShot(0, this, [this] { fail(tr("EcoFlow access key and secret key are required")); });
        return;
    }
    client->get(kDeviceListPath, {}, [this](const ecoflow::EcoFlowClient::Result& result) {
        if (!result) {
            fail(result.error().message);
            return;
        }
        QList<DiscoveredDevice> found;
        for (const QJsonValue& v : result->toArray()) {
            const QJsonObject o = v.toObject();
            DiscoveredDevice d;
            d.id = o.value(u"sn").toString();
            if (d.id.isEmpty())
                continue;
            const QString product = o.value(u"productName").toString();
            const QString name = o.value(u"deviceName").toString();
            d.name = !name.isEmpty() ? name : (!product.isEmpty() ? product : d.id);
            d.description = product;
            d.online = isOnline(o.value(u"online"));
            found << d;
        }
        finish(found);
    });
}

}  // namespace nutty
