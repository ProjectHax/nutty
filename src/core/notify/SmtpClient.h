// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>

class QSslSocket;
class QTimer;

namespace nutty {

// A minimal async SMTP submission client: plain, STARTTLS or implicit TLS,
// AUTH PLAIN/LOGIN, one message per send(). Emits finished() exactly once
// per send().
class SmtpClient : public QObject
{
    Q_OBJECT

public:
    enum class Security {
        None,
        StartTls,
        Tls,
    };

    struct Settings {
        QString host;
        quint16 port = 587;
        Security security = Security::StartTls;
        QString username;  // empty = no AUTH
        QString password;
        int timeoutMs = 30000;
        QString heloName = QStringLiteral("localhost");
    };

    struct Message {
        QString from;
        QStringList to;
        QString subject;
        QString body;
        QDateTime date;  // invalid = now
        QString messageIdDomain = QStringLiteral("nutty.invalid");
    };

    explicit SmtpClient(QObject* parent = nullptr);
    ~SmtpClient() override;

    void send(const Settings& settings, const Message& message);
    bool isBusy() const { return m_busy; }

    // The full RFC 5322 message (CRLF line endings, quoted-printable UTF-8
    // body), before dot-stuffing.
    static QByteArray buildMessage(const Message& message);
    static QByteArray quotedPrintable(const QByteArray& utf8);
    // Doubles a leading '.' on every line (RFC 5321 section 4.5.2).
    static QByteArray dotStuff(const QByteArray& data);
    // "Name <a@b.c>" -> "a@b.c"; a bare address is returned trimmed.
    static QString extractAddress(const QString& mailbox);

signals:
    void finished(bool ok, const QString& error);

private:
    enum class Step {
        Idle,
        Greeting,
        Ehlo,
        Helo,
        StartTls,
        TlsHandshake,
        AuthPlain,
        AuthLogin,
        AuthLoginUser,
        AuthLoginPassword,
        MailFrom,
        RcptTo,
        Data,
        DataBody,
        Quit,
    };

    void onReadyRead();
    void handleReply(int code, const QStringList& lines);
    void sendCommand(const QByteArray& command, Step next);
    void startAuthentication();
    void sendMailFrom();
    void fail(const QString& error);
    void succeed();
    void finish(bool ok, const QString& error);

    QSslSocket* m_socket = nullptr;
    QTimer* m_timeout = nullptr;
    Settings m_settings;
    Message m_message;
    Step m_step = Step::Idle;
    QByteArray m_buffer;
    QStringList m_replyLines;
    QStringList m_capabilities;
    qsizetype m_recipientIndex = 0;
    QStringList m_recipients;
    QString m_lastSslError;
    bool m_tlsActive = false;
    bool m_busy = false;
    bool m_done = false;
};

}  // namespace nutty
