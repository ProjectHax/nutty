// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Source.h"
#include "core/sources/apcupsd/ApcupsdProtocol.h"

#include <QPointer>
#include <QTimer>

class QTcpSocket;

namespace nutty {

// Polls an apcupsd NIS server: one short connection per poll ("status").
class ApcupsdSource : public Source
{
    Q_OBJECT

public:
    explicit ApcupsdSource(DeviceConfig config, QObject* parent = nullptr);
    ~ApcupsdSource() override;

    void start() override;
    void stop() override;
    void pollNow() override;
    QString describe() const override;

    static constexpr int kTimeoutMs = 10000;

private:
    void poll();
    void finishPoll();
    void abortPoll(SourceError kind, const QString& message);

    QString m_host;
    quint16 m_port = apcupsd::kDefaultPort;
    QTimer m_pollTimer;
    QTimer m_timeout;
    QPointer<QTcpSocket> m_socket;
    apcupsd::RecordReader m_reader;
    bool m_running = false;
};

}  // namespace nutty
