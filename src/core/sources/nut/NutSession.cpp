// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/sources/nut/NutSession.h"

#include "core/model/Config.h"

#include <QCryptographicHash>
#include <QSslCertificate>
#include <QSslSocket>

namespace nutty::nut {

namespace {

QString normalizedFingerprint(QString fp)
{
    fp.remove(u':');
    fp.remove(u' ');
    return fp.toLower();
}

}  // namespace

SessionOptions SessionOptions::fromSettings(const QVariantMap& settings, const QVariantMap& secrets)
{
    SessionOptions o;
    const QString host = settings.value(QLatin1StringView(keys::NutHost)).toString().trimmed();
    if (!host.isEmpty())
        o.host = host;
    const int port = settings.value(QLatin1StringView(keys::NutPort)).toInt();
    if (port > 0 && port <= 65535)
        o.port = static_cast<quint16>(port);
    o.requireTls = settings.value(QLatin1StringView(keys::NutTls)).toString() == u"required";
    o.fingerprint = settings.value(QLatin1StringView(keys::NutFingerprint)).toString().trimmed();
    o.username = settings.value(QLatin1StringView(keys::NutUsername)).toString().trimmed();
    o.password = secrets.value(QLatin1StringView(keys::NutPassword)).toString();
    return o;
}

NutSession::NutSession(QObject* parent)
    : QObject(parent)
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this,
        [this] { fail(SourceError::Network, tr("The NUT server did not respond in time")); });
}

NutSession::~NutSession()
{
    close();
}

bool NutSession::isEncrypted() const
{
    return m_socket && m_socket->isEncrypted();
}

void NutSession::open(const SessionOptions& options)
{
    close();
    m_options = options;
    m_failed = false;
    m_closing = false;
    m_ready = false;

    auto* socket = new QSslSocket(this);
    m_socket = socket;
    connect(socket, &QSslSocket::connected, this, &NutSession::onConnected);
    connect(socket, &QSslSocket::encrypted, this, &NutSession::onEncrypted);
    connect(socket, &QSslSocket::readyRead, this, &NutSession::onReadyRead);
    connect(socket, &QSslSocket::sslErrors, this, &NutSession::onSslErrors);
    connect(socket, &QSslSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError error) {
        if (m_closing || socket != m_socket)
            return;
        const bool tls = error == QAbstractSocket::SslHandshakeFailedError
            || error == QAbstractSocket::SslInternalError
            || error == QAbstractSocket::SslInvalidUserDataError;
        if (error == QAbstractSocket::RemoteHostClosedError)
            fail(SourceError::Network, tr("The NUT server closed the connection"));
        else
            fail(tls ? SourceError::Tls : SourceError::Network, socket->errorString());
    });
    connect(socket, &QSslSocket::disconnected, this, [this, socket] {
        if (!m_closing && socket == m_socket)
            fail(SourceError::Network, tr("The NUT server closed the connection"));
    });

    m_timeout.start(m_options.timeoutMs);
    socket->connectToHost(m_options.host, m_options.port);
}

void NutSession::close()
{
    m_timeout.stop();
    m_queue.clear();
    m_written = false;
    m_ready = false;
    if (m_socket) {
        m_closing = true;
        QSslSocket* socket = m_socket;
        m_socket = nullptr;
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }
}

void NutSession::onConnected()
{
    m_timeout.stop();
    if (!m_options.requireTls) {
        authenticate();
        return;
    }
    if (!QSslSocket::supportsSsl()) {
        fail(SourceError::Tls, tr("TLS is not available on this system"));
        return;
    }
    // STARTTLS goes first, before any credentials cross the wire.
    sendSimple({ QStringLiteral("STARTTLS") }, [this](const Line& reply) {
        if (reply.kind != LineKind::Ok) {
            fail(SourceError::Tls,
                reply.kind == LineKind::Err ? errorDescription(reply.errorCode)
                                            : tr("The NUT server refused STARTTLS"));
            return;
        }
        m_socket->setPeerVerifyName(m_options.host);
        m_timeout.start(m_options.timeoutMs);
        m_socket->startClientEncryption();
    });
}

void NutSession::onEncrypted()
{
    m_timeout.stop();
    if (!m_options.fingerprint.isEmpty() && !fingerprintMatches()) {
        fail(SourceError::Tls, tr("The NUT server's certificate doesn't match the pinned fingerprint"));
        return;
    }
    authenticate();
}

bool NutSession::fingerprintMatches() const
{
    if (!m_socket)
        return false;
    const QByteArray digest = m_socket->peerCertificate().digest(QCryptographicHash::Sha256).toHex();
    return QString::fromLatin1(digest) == normalizedFingerprint(m_options.fingerprint);
}

void NutSession::onSslErrors(const QList<QSslError>& errors)
{
    // A self-signed (or otherwise untrusted) certificate is acceptable only
    // when the user pinned exactly this certificate.
    if (!m_options.fingerprint.isEmpty() && fingerprintMatches())
        m_socket->ignoreSslErrors(errors);
}

void NutSession::authenticate()
{
    if (m_options.username.isEmpty()) {
        m_ready = true;
        emit ready();
        return;
    }
    sendSimple({ QStringLiteral("USERNAME"), m_options.username }, [this](const Line& reply) {
        if (reply.kind == LineKind::Err) {
            fail(errorKind(reply.errorCode), errorDescription(reply.errorCode));
            return;
        }
        sendSimple({ QStringLiteral("PASSWORD"), m_options.password }, [this](const Line& reply) {
            if (reply.kind == LineKind::Err) {
                fail(errorKind(reply.errorCode), errorDescription(reply.errorCode));
                return;
            }
            m_ready = true;
            emit ready();
        });
    });
}

void NutSession::sendSimple(const QStringList& words, ReplyHandler handler)
{
    Request r;
    r.data = command(words);
    r.simple = std::move(handler);
    m_queue.push_back(std::move(r));
    writeNext();
}

void NutSession::sendList(const QStringList& words, ListHandler handler)
{
    Request r;
    r.data = command(words);
    r.isList = true;
    r.list = std::move(handler);
    m_queue.push_back(std::move(r));
    writeNext();
}

void NutSession::sendAndForget(const QStringList& words)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
        return;
    m_socket->write(command(words));
    m_socket->flush();
}

void NutSession::writeNext()
{
    if (m_written || m_queue.empty() || !m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
        return;
    m_written = true;
    m_socket->write(m_queue.front().data);
    m_timeout.start(m_options.timeoutMs);
}

void NutSession::onReadyRead()
{
    // A handler may close this session (fail/close), so re-check the socket on
    // every iteration.
    while (m_socket && m_socket->canReadLine()) {
        const QString text = QString::fromUtf8(m_socket->readLine()).trimmed();
        if (!text.isEmpty())
            handleLine(text);
    }
}

void NutSession::handleLine(const QString& text)
{
    if (m_queue.empty() || !m_written)
        return;  // unsolicited; upsd doesn't send any, so ignore it

    Request& head = m_queue.front();
    const Line line = parseLine(text);

    if (!head.isList) {
        Request done = std::move(head);
        m_queue.pop_front();
        m_written = false;
        m_timeout.stop();
        if (done.simple)
            done.simple(line);
        writeNext();
        return;
    }

    if (!head.inList) {
        if (line.kind == LineKind::Begin) {
            head.inList = true;
            m_timeout.start(m_options.timeoutMs);
            return;
        }
        Request done = std::move(head);
        m_queue.pop_front();
        m_written = false;
        m_timeout.stop();
        if (done.list)
            done.list(line.kind == LineKind::Err ? line.errorCode : QStringLiteral("UNEXPECTED-REPLY"), {});
        writeNext();
        return;
    }

    if (line.kind == LineKind::End) {
        Request done = std::move(head);
        m_queue.pop_front();
        m_written = false;
        m_timeout.stop();
        if (done.list)
            done.list(std::nullopt, done.items);
        writeNext();
        return;
    }
    head.items << line.words;
}

void NutSession::fail(SourceError kind, const QString& message)
{
    if (m_failed)
        return;
    m_failed = true;
    close();
    emit failed(kind, message);
}

}  // namespace nutty::nut
