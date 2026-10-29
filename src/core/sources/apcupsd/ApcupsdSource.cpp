// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/sources/apcupsd/ApcupsdSource.h"

#include <QTcpSocket>

namespace nutty {

ApcupsdSource::ApcupsdSource(DeviceConfig config, QObject* parent)
    : Source(std::move(config), parent)
{
    const auto& settings = this->config().settings;
    m_host = settings.value(QLatin1StringView(keys::ApcHost)).toString().trimmed();
    if (m_host.isEmpty())
        m_host = QStringLiteral("localhost");
    const int port = settings.value(QLatin1StringView(keys::ApcPort)).toInt();
    if (port > 0 && port <= 65535)
        m_port = static_cast<quint16>(port);

    connect(&m_pollTimer, &QTimer::timeout, this, &ApcupsdSource::poll);
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this,
        [this] { abortPoll(SourceError::Network, tr("apcupsd did not respond in time")); });
}

ApcupsdSource::~ApcupsdSource()
{
    stop();
}

QString ApcupsdSource::describe() const
{
    return QStringLiteral("%1:%2").arg(m_host).arg(m_port);
}

void ApcupsdSource::start()
{
    m_running = true;
    setConnectionState(ConnectionState::Connecting);
    m_pollTimer.start(pollIntervalMs());
    poll();
}

void ApcupsdSource::stop()
{
    m_running = false;
    m_pollTimer.stop();
    m_timeout.stop();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    setConnectionState(ConnectionState::Disconnected);
}

void ApcupsdSource::pollNow()
{
    poll();
}

void ApcupsdSource::poll()
{
    if (!m_running || m_socket)
        return;  // not running, or the previous poll is still in flight

    m_reader.clear();
    auto* socket = new QTcpSocket(this);
    m_socket = socket;
    connect(
        socket, &QTcpSocket::connected, this, [socket] { socket->write(apcupsd::encodeRecord("status")); });
    connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
        m_reader.append(socket->readAll());
        if (m_reader.isComplete())
            finishPoll();
    });
    connect(socket, &QTcpSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError) {
        if (socket == m_socket && !m_reader.isComplete())
            abortPoll(SourceError::Network, socket->errorString());
    });
    m_timeout.start(kTimeoutMs);
    socket->connectToHost(m_host, m_port);
}

void ApcupsdSource::finishPoll()
{
    m_timeout.stop();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->disconnectFromHost();
        m_socket->deleteLater();
        m_socket = nullptr;
    }

    const auto values = apcupsd::parseStatusRecords(m_reader.records());
    if (values.isEmpty()) {
        reportError(SourceError::Protocol, tr("apcupsd returned no status"));
        return;
    }
    auto result = apcupsd::statusFromValues(values);
    if (result.commLost) {
        reportError(SourceError::DataStale, tr("apcupsd has lost contact with the UPS"));
        return;
    }
    publish(std::move(result.status));
}

void ApcupsdSource::abortPoll(SourceError kind, const QString& message)
{
    m_timeout.stop();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    reportError(kind, message);
}

}  // namespace nutty
