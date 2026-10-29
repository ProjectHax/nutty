// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Source.h"
#include "core/sources/nut/NutSession.h"

#include <QPointer>
#include <QTimer>

namespace nutty {

// Monitors one UPS on a NUT server over a persistent connection, polling
// `LIST VAR <ups>`. Optionally logs in as an upsmon secondary so the primary
// waits for this machine before cutting the UPS power.
class NutSource : public Source
{
    Q_OBJECT

public:
    explicit NutSource(DeviceConfig config, QObject* parent = nullptr);
    ~NutSource() override;

    void start() override;
    void stop() override;
    void pollNow() override;
    void prepareForShutdown() override;
    QString describe() const override;

    bool isLoggedIn() const { return m_loggedIn; }

    // Reconnect delays, in milliseconds (exposed for tests).
    static constexpr int kBackoffMs[] = { 1000, 2000, 5000, 10000, 30000 };

private:
    void connectSession();
    void onSessionReady();
    void onSessionFailed(SourceError kind, const QString& message);
    void startPolling();
    void poll();
    void scheduleReconnect();

    nut::SessionOptions m_options;
    QString m_ups;
    bool m_secondary = false;

    QPointer<nut::NutSession> m_session;
    QTimer m_pollTimer;
    QTimer m_reconnectTimer;
    int m_backoffIndex = 0;
    bool m_running = false;
    bool m_polling = false;
    bool m_loggedIn = false;
    bool m_shuttingDown = false;
};

// Lists the UPS units a NUT server offers (`LIST UPS`).
class NutDiscovery : public Discovery
{
    Q_OBJECT

public:
    NutDiscovery(const QVariantMap& settings, const QVariantMap& secrets, QObject* parent = nullptr);

private:
    nut::NutSession* m_session = nullptr;
};

}  // namespace nutty
