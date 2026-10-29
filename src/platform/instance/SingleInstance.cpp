// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/instance/SingleInstance.h"

#include <QCryptographicHash>
#include <QDeadlineTimer>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QtEndian>

#include <memory>

// Wire format: a 4-byte big-endian length, then the message. The primary
// answers with one 'k' byte once it has emitted messageReceived().

namespace nutty {

namespace {

Q_LOGGING_CATEGORY(lcInstance, "nutty.instance")

constexpr quint32 kMaxMessageSize = 64 * 1024;
constexpr char kAck = 'k';

}  // namespace

QString SingleInstance::runtimeDirectory()
{
#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)
    // Each Flatpak instance gets a private /run/user/<uid>; the app's own
    // subdirectory is the part that's shared between them.
    const auto flatpakId = qEnvironmentVariable("FLATPAK_ID");
    const auto xdgRuntime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!flatpakId.isEmpty() && !xdgRuntime.isEmpty())
        return xdgRuntime + QStringLiteral("/app/") + flatpakId;
    const auto runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (!runtime.isEmpty())
        return runtime;
#endif
    // Per-user on Windows and macOS.
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation);
}

SingleInstance::SingleInstance(const QString& key, QObject* parent)
    : QObject(parent)
{
    const auto dir = runtimeDirectory();
    QDir().mkpath(dir);
#ifdef Q_OS_WIN
    // Named pipes share one namespace across users.
    const auto user
        = QCryptographicHash::hash(QDir::homePath().toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
    m_serverName = key + u'-' + QString::fromLatin1(user);
#else
    m_serverName = dir + u'/' + key + QStringLiteral(".socket");
#endif

    m_lock = std::make_unique<QLockFile>(dir + u'/' + key + QStringLiteral(".lock"));
    // Only a dead owner makes the lock stale, never its age.
    m_lock->setStaleLockTime(0);
    m_primary = m_lock->tryLock(0);
    if (!m_primary && m_lock->error() != QLockFile::LockFailedError) {
        // The lock can't be created at all; running twice beats not running.
        qCWarning(lcInstance) << "cannot create the instance lock in" << dir;
        m_primary = true;
    }
    if (m_primary)
        listen();
}

SingleInstance::~SingleInstance()
{
    if (m_server)
        m_server->close();
}

void SingleInstance::listen()
{
    // We hold the lock, so any socket left behind belongs to a dead instance.
    QLocalServer::removeServer(m_serverName);
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    connect(m_server, &QLocalServer::newConnection, this, &SingleInstance::acceptConnections);
    if (!m_server->listen(m_serverName))
        qCWarning(lcInstance) << "cannot listen for other instances:" << m_server->errorString();
}

void SingleInstance::acceptConnections()
{
    while (auto* socket = m_server->nextPendingConnection()) {
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QLocalSocket::readyRead, this, [this, socket, buffer] {
            buffer->append(socket->readAll());
            if (buffer->size() < 4)
                return;
            const auto length = qFromBigEndian<quint32>(buffer->constData());
            if (length > kMaxMessageSize) {
                socket->abort();
                return;
            }
            if (quint32(buffer->size()) < 4 + length)
                return;
            const auto message = buffer->mid(4, length);
            buffer->clear();
            socket->write(&kAck, 1);
            socket->disconnectFromServer();
            emit messageReceived(message);
        });
    }
}

bool SingleInstance::sendToPrimary(const QByteArray& message, int timeoutMs)
{
    if (m_primary || quint32(message.size()) > kMaxMessageSize)
        return false;

    QDeadlineTimer deadline(timeoutMs);
    QLocalSocket socket;
    socket.connectToServer(m_serverName);
    if (!socket.waitForConnected(int(deadline.remainingTime())))
        return false;

    QByteArray frame(4, Qt::Uninitialized);
    qToBigEndian<quint32>(quint32(message.size()), frame.data());
    frame += message;
    socket.write(frame);
    if (!socket.waitForBytesWritten(int(deadline.remainingTime())))
        return false;
    while (socket.bytesAvailable() < 1) {
        if (deadline.hasExpired() || !socket.waitForReadyRead(int(deadline.remainingTime())))
            return false;
    }
    return socket.read(1) == QByteArray(1, kAck);
}

}  // namespace nutty
