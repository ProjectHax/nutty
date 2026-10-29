// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Types.h"
#include "core/sources/nut/NutProtocol.h"

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSslError>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include <deque>
#include <functional>
#include <optional>

class QSslSocket;

namespace nutty::nut {

struct SessionOptions {
    QString host = QStringLiteral("localhost");
    quint16 port = kDefaultPort;
    bool requireTls = false;
    QString fingerprint;  // pinned SHA-256 of the server certificate (hex, ':' allowed)
    QString username;
    QString password;
    int timeoutMs = 10000;

    // Reads the NUT keys:: from a device's settings/secrets.
    static SessionOptions fromSettings(const QVariantMap& settings, const QVariantMap& secrets);
};

// One connection to upsd. open() connects, negotiates STARTTLS when required
// (never falling back to plaintext) and sends USERNAME/PASSWORD, then emits
// ready(). Commands are queued and sent one at a time; every reply is handed
// to the command's handler. Any transport failure emits failed() once and
// leaves the session closed.
class NutSession : public QObject
{
    Q_OBJECT

public:
    // Ok/Err/Other reply line.
    using ReplyHandler = std::function<void(const Line& reply)>;
    // error is the ERR code if the list was refused; items are the tokenized
    // lines between BEGIN LIST and END LIST.
    using ListHandler
        = std::function<void(const std::optional<QString>& error, const QList<QStringList>& items)>;

    explicit NutSession(QObject* parent = nullptr);
    ~NutSession() override;

    void open(const SessionOptions& options);
    void close();
    bool isReady() const { return m_ready; }
    bool isEncrypted() const;

    void sendSimple(const QStringList& words, ReplyHandler handler);
    void sendList(const QStringList& words, ListHandler handler);
    // Writes a command without waiting for a reply (used for LOGOUT).
    void sendAndForget(const QStringList& words);

signals:
    void ready();
    void failed(nutty::SourceError kind, const QString& message);

private:
    struct Request {
        QByteArray data;
        bool isList = false;
        bool inList = false;
        ReplyHandler simple;
        ListHandler list;
        QList<QStringList> items;
    };

    void onConnected();
    void onEncrypted();
    void onReadyRead();
    void onSslErrors(const QList<QSslError>& errors);
    void handleLine(const QString& text);
    void authenticate();
    void writeNext();
    void fail(SourceError kind, const QString& message);
    bool fingerprintMatches() const;

    SessionOptions m_options;
    QPointer<QSslSocket> m_socket;
    std::deque<Request> m_queue;
    bool m_written = false;  // the head of the queue has been sent
    bool m_ready = false;
    bool m_failed = false;
    bool m_closing = false;
    QTimer m_timeout;
};

}  // namespace nutty::nut
