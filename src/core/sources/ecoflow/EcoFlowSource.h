// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Source.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QTimer>

#include <optional>

namespace nutty {

namespace ecoflow {
class EcoFlowClient;
}

// Polls one EcoFlow device through the IoT Open Platform (quota/all), and
// refreshes its online flag and product name from device/list now and then.
// A reading that fails is tried again a few times before the error is
// reported. Changes to the device's settings go out one at a time as a PUT to
// device/quota; the source then polls until the device reports the new value.
class EcoFlowSource : public Source
{
    Q_OBJECT

public:
    explicit EcoFlowSource(DeviceConfig config, QObject* parent = nullptr);
    ~EcoFlowSource() override;

    void start() override;
    void stop() override;
    void pollNow() override;
    QString describe() const override;
    DeviceDetails details() const override { return m_details; }
    QList<DeviceControl> controls() const override;
    bool applyControl(const QString& id, double value, QString* error = nullptr) override;

    static constexpr int kDeviceInfoRefreshSec = 600;
    // Tries after a failed reading, and the wait before each. At the default
    // 30 s poll they all fit before the deadtime (three polls) runs out, even
    // when every request times out.
    static constexpr int kRetries = 3;
    static constexpr int kRetryDelayMs = 2000;
    // After EcoFlow takes a change: how often to look for it in the
    // readings, and for how long before calling it unconfirmed.
    static constexpr int kConfirmPollMs = 4000;
    static constexpr int kConfirmTimeoutMs = 30000;
    // Shorter waits, for tests.
    void setConfirmTiming(int pollMs, int timeoutMs);
    void setRetryDelay(int ms) { m_retryTimer.setInterval(ms); }

private:
    struct Pending {
        QString id;
        double value = 0;
        bool sent = false;  // EcoFlow accepted it
        QElapsedTimer since;
    };

    void poll();
    // One try at a reading: device/list first when it's due, then quota/all.
    void fetch();
    void refreshDeviceInfo();
    void fetchQuota();
    // Retries the reading, or reports the error once the tries run out.
    void fail(SourceError kind, const QString& message);
    void checkPending();
    void finishPending(ControlResult result, const QString& error = {});

    ecoflow::EcoFlowClient* m_client = nullptr;
    QString m_serial;
    QTimer m_pollTimer;
    bool m_running = false;
    bool m_inFlight = false;  // a reading is under way, retries included
    int m_retriesLeft = 0;
    QTimer m_retryTimer;
    std::optional<bool> m_online;
    QString m_productName;
    QDateTime m_infoFetchedAt;
    QJsonObject m_quota;  // the last readout
    DeviceDetails m_details;
    std::optional<Pending> m_pending;
    QTimer m_confirmTimer;
    int m_confirmTimeoutMs = kConfirmTimeoutMs;
};

// Lists the devices bound to an EcoFlow developer account (device/list).
class EcoFlowDiscovery : public Discovery
{
    Q_OBJECT

public:
    EcoFlowDiscovery(const QVariantMap& settings, const QVariantMap& secrets, QObject* parent = nullptr);
};

}  // namespace nutty
