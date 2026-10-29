// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <memory>

class QLocalServer;
class QLockFile;

namespace nutty {

// Keeps one nutty per user session. The first instance holds a lock file and
// listens on a local socket; later ones find the lock taken and can hand
// their request (e.g. "show") to the first through sendToPrimary().
class SingleInstance : public QObject
{
    Q_OBJECT

public:
    // key names the lock and the socket; tests pass their own.
    explicit SingleInstance(const QString& key = QStringLiteral(NUTTY_APP_ID), QObject* parent = nullptr);
    ~SingleInstance() override;

    bool isPrimary() const { return m_primary; }

    // From a secondary instance: delivers message to the primary's
    // messageReceived() and waits for it to confirm. False on timeout or
    // when no primary is listening.
    bool sendToPrimary(const QByteArray& message, int timeoutMs = 3000);

    // Where the lock and socket live (the per-user runtime directory).
    static QString runtimeDirectory();

signals:
    void messageReceived(const QByteArray& message);

private:
    void listen();
    void acceptConnections();

    QString m_serverName;
    std::unique_ptr<QLockFile> m_lock;
    QLocalServer* m_server = nullptr;
    bool m_primary = false;
};

}  // namespace nutty
