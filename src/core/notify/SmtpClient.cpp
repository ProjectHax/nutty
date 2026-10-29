// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/notify/SmtpClient.h"

#include "core/notify/NotifyUtil.h"

#include <QLocale>
#include <QSslSocket>
#include <QTimer>
#include <QUuid>

namespace nutty {

SmtpClient::SmtpClient(QObject* parent)
    : QObject(parent)
    , m_timeout(new QTimer(this))
{
    m_timeout->setSingleShot(true);
    connect(
        m_timeout, &QTimer::timeout, this, [this]() { fail(tr("Timed out waiting for the mail server.")); });
}

SmtpClient::~SmtpClient() = default;

void SmtpClient::send(const Settings& settings, const Message& message)
{
    if (m_busy) {
        emit finished(false, tr("Another message is still being sent."));
        return;
    }
    m_settings = settings;
    m_message = message;
    m_recipients.clear();
    for (const auto& to : message.to) {
        const auto address = extractAddress(to);
        if (!address.isEmpty())
            m_recipients << address;
    }
    if (m_settings.host.isEmpty()) {
        emit finished(false, tr("No mail server is configured."));
        return;
    }
    if (m_recipients.isEmpty()) {
        emit finished(false, tr("No recipient address is configured."));
        return;
    }
    if (m_settings.security != Security::None && !QSslSocket::supportsSsl()) {
        emit finished(false, tr("TLS is not available on this system."));
        return;
    }

    m_busy = true;
    m_done = false;
    m_tlsActive = false;
    m_buffer.clear();
    m_replyLines.clear();
    m_capabilities.clear();
    m_recipientIndex = 0;
    m_lastSslError.clear();

    if (m_socket) {
        // The previous connection may still be closing; its signals must not
        // reach this send.
        m_socket->disconnect(this);
        m_socket->deleteLater();
    }
    m_socket = new QSslSocket(this);
    connect(m_socket, &QSslSocket::readyRead, this, &SmtpClient::onReadyRead);
    connect(m_socket, &QSslSocket::encrypted, this, [this]() {
        m_tlsActive = true;
        if (m_step == Step::TlsHandshake)
            sendCommand("EHLO " + m_settings.heloName.toUtf8(), Step::Ehlo);
    });
    connect(m_socket, &QSslSocket::sslErrors, this, [this](const QList<QSslError>& errors) {
        if (!errors.isEmpty())
            m_lastSslError = errors.first().errorString();
    });
    connect(m_socket, &QAbstractSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
        if (m_done)
            return;
        if (error == QAbstractSocket::RemoteHostClosedError && m_step == Step::Quit) {
            succeed();
            return;
        }
        fail(m_lastSslError.isEmpty() ? m_socket->errorString() : m_lastSslError);
    });

    m_step = Step::Greeting;
    m_timeout->start(m_settings.timeoutMs);
    if (m_settings.security == Security::Tls)
        m_socket->connectToHostEncrypted(m_settings.host, m_settings.port);
    else
        m_socket->connectToHost(m_settings.host, m_settings.port);
}

void SmtpClient::onReadyRead()
{
    m_buffer += m_socket->readAll();
    while (true) {
        const auto newline = m_buffer.indexOf('\n');
        if (newline < 0)
            break;
        QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.size() < 3) {
            fail(tr("The mail server sent an invalid reply."));
            return;
        }
        bool ok = false;
        const int code = line.left(3).toInt(&ok);
        if (!ok) {
            fail(tr("The mail server sent an invalid reply."));
            return;
        }
        m_replyLines << QString::fromUtf8(line.mid(4));
        // "250-..." continues a multi-line reply; "250 ..." ends it.
        if (line.size() > 3 && line.at(3) == '-')
            continue;
        const auto lines = std::exchange(m_replyLines, {});
        handleReply(code, lines);
        if (m_done)
            return;
    }
}

void SmtpClient::sendCommand(const QByteArray& command, Step next)
{
    m_step = next;
    m_timeout->start(m_settings.timeoutMs);
    m_socket->write(command + "\r\n");
}

void SmtpClient::handleReply(int code, const QStringList& lines)
{
    const auto serverText = lines.join(u' ').trimmed();
    const auto rejected = [&](const QString& what) {
        fail(QStringLiteral("%1 (%2 %3)").arg(what).arg(code).arg(serverText));
    };

    switch (m_step) {
    case Step::Greeting:
        if (code != 220)
            return rejected(tr("The mail server refused the connection"));
        sendCommand("EHLO " + m_settings.heloName.toUtf8(), Step::Ehlo);
        return;

    case Step::Ehlo:
        if (code != 250) {
            // Very old servers only speak HELO; that rules out TLS and AUTH.
            if (m_settings.security == Security::None && m_settings.username.isEmpty())
                return sendCommand("HELO " + m_settings.heloName.toUtf8(), Step::Helo);
            return rejected(tr("The mail server rejected EHLO"));
        }
        m_capabilities = lines.mid(1);
        if (m_settings.security == Security::StartTls && !m_tlsActive) {
            const bool offered = std::ranges::any_of(m_capabilities, [](const QString& cap) {
                return cap.trimmed().compare(QLatin1StringView("STARTTLS"), Qt::CaseInsensitive) == 0;
            });
            if (!offered)
                return fail(tr("The mail server does not offer STARTTLS."));
            return sendCommand("STARTTLS", Step::StartTls);
        }
        return startAuthentication();

    case Step::Helo:
        if (code != 250)
            return rejected(tr("The mail server rejected HELO"));
        return sendMailFrom();

    case Step::StartTls:
        if (code != 220)
            return rejected(tr("The mail server refused STARTTLS"));
        m_step = Step::TlsHandshake;
        m_timeout->start(m_settings.timeoutMs);
        m_socket->startClientEncryption();
        return;

    case Step::TlsHandshake:
        return;

    case Step::AuthPlain:
    case Step::AuthLoginPassword:
        if (code != 235)
            return rejected(tr("The mail server rejected the username or password"));
        return sendMailFrom();

    case Step::AuthLogin:
        if (code != 334)
            return rejected(tr("The mail server rejected AUTH LOGIN"));
        return sendCommand(m_settings.username.toUtf8().toBase64(), Step::AuthLoginUser);

    case Step::AuthLoginUser:
        if (code != 334)
            return rejected(tr("The mail server rejected the username"));
        return sendCommand(m_settings.password.toUtf8().toBase64(), Step::AuthLoginPassword);

    case Step::MailFrom:
        if (code != 250)
            return rejected(tr("The mail server rejected the sender address"));
        m_recipientIndex = 0;
        return sendCommand("RCPT TO:<" + m_recipients.at(0).toUtf8() + '>', Step::RcptTo);

    case Step::RcptTo:
        if (code != 250 && code != 251)
            return rejected(
                tr("The mail server rejected the recipient %1").arg(m_recipients.value(m_recipientIndex)));
        if (++m_recipientIndex < m_recipients.size())
            return sendCommand("RCPT TO:<" + m_recipients.at(m_recipientIndex).toUtf8() + '>', Step::RcptTo);
        return sendCommand("DATA", Step::Data);

    case Step::Data: {
        if (code != 354)
            return rejected(tr("The mail server refused the message"));
        QByteArray payload = dotStuff(buildMessage(m_message));
        if (!payload.endsWith("\r\n"))
            payload += "\r\n";
        payload += ".";
        return sendCommand(payload, Step::DataBody);
    }

    case Step::DataBody:
        if (code != 250)
            return rejected(tr("The mail server refused the message"));
        // The message is accepted; QUIT is a courtesy. Report last: a
        // finished() handler may start the next send() on this client.
        m_step = Step::Quit;
        m_socket->write("QUIT\r\n");
        m_socket->disconnectFromHost();
        succeed();
        return;

    case Step::Quit:
    case Step::Idle:
        return;
    }
}

void SmtpClient::startAuthentication()
{
    if (m_settings.username.isEmpty())
        return sendMailFrom();

    QStringList mechanisms;
    for (const auto& cap : std::as_const(m_capabilities)) {
        const auto upper = cap.trimmed().toUpper();
        if (upper.startsWith(u"AUTH ") || upper.startsWith(u"AUTH="))
            mechanisms << upper.mid(5).split(u' ', Qt::SkipEmptyParts);
    }
    if (mechanisms.contains(u"PLAIN")) {
        QByteArray credentials(1, '\0');
        credentials += m_settings.username.toUtf8();
        credentials += '\0';
        credentials += m_settings.password.toUtf8();
        return sendCommand("AUTH PLAIN " + credentials.toBase64(), Step::AuthPlain);
    }
    if (mechanisms.contains(u"LOGIN"))
        return sendCommand("AUTH LOGIN", Step::AuthLogin);
    fail(tr("The mail server offers no supported login method (PLAIN or LOGIN)."));
}

void SmtpClient::sendMailFrom()
{
    sendCommand("MAIL FROM:<" + extractAddress(m_message.from).toUtf8() + '>', Step::MailFrom);
}

void SmtpClient::fail(const QString& error)
{
    if (m_done)
        return;
    // Abort first: a finished() handler may start the next send().
    if (m_socket)
        m_socket->abort();
    finish(false, error);
}

void SmtpClient::succeed()
{
    finish(true, {});
}

void SmtpClient::finish(bool ok, const QString& error)
{
    if (m_done)
        return;
    m_done = true;
    m_busy = false;
    m_timeout->stop();
    emit finished(ok, error);
}

// --- message building -------------------------------------------------------

QString SmtpClient::extractAddress(const QString& mailbox)
{
    const auto open = mailbox.lastIndexOf(u'<');
    const auto close = mailbox.lastIndexOf(u'>');
    if (open >= 0 && close > open)
        return mailbox.mid(open + 1, close - open - 1).trimmed();
    return mailbox.trimmed();
}

namespace {

// "Näme <a@b.c>" -> "=?UTF-8?B?...?= <a@b.c>"
QByteArray encodeMailbox(const QString& mailbox)
{
    const auto open = mailbox.lastIndexOf(u'<');
    if (open <= 0)
        return mailbox.trimmed().toUtf8();
    const auto name = mailbox.left(open).trimmed();
    return notify::encodeHeaderWord(name) + ' ' + mailbox.mid(open).trimmed().toUtf8();
}

}  // namespace

QByteArray SmtpClient::buildMessage(const Message& message)
{
    const auto date = (message.date.isValid() ? message.date : QDateTime::currentDateTimeUtc()).toUTC();
    QByteArrayList recipients;
    for (const auto& to : message.to) {
        if (!to.trimmed().isEmpty())
            recipients << encodeMailbox(to);
    }

    QByteArray out;
    out += "Date: " + QLocale::c().toString(date, QStringLiteral("ddd, dd MMM yyyy HH:mm:ss")).toLatin1()
        + " +0000\r\n";
    out += "From: " + encodeMailbox(message.from) + "\r\n";
    out += "To: " + recipients.join(", ") + "\r\n";
    out += "Subject: " + notify::encodeHeaderWord(message.subject) + "\r\n";
    out += "Message-ID: <" + QUuid::createUuid().toString(QUuid::WithoutBraces).toLatin1() + '@'
        + message.messageIdDomain.toLatin1() + ">\r\n";
    out += "MIME-Version: 1.0\r\n";
    out += "Content-Type: text/plain; charset=UTF-8\r\n";
    out += "Content-Transfer-Encoding: quoted-printable\r\n";
    out += "X-Mailer: nutty/" NUTTY_VERSION "\r\n";
    out += "\r\n";
    out += quotedPrintable(message.body.toUtf8());
    return out;
}

QByteArray SmtpClient::quotedPrintable(const QByteArray& utf8)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    constexpr qsizetype kMaxLine = 75;  // plus the '=' of a soft break = 76

    QByteArray normalized = utf8;
    normalized.replace("\r\n", "\n");
    const auto lines = normalized.split('\n');

    QByteArray out;
    for (qsizetype n = 0; n < lines.size(); ++n) {
        const auto& line = lines.at(n);
        qsizetype width = 0;
        for (qsizetype i = 0; i < line.size(); ++i) {
            const auto c = static_cast<unsigned char>(line.at(i));
            const bool last = i == line.size() - 1;
            QByteArray token;
            if ((c >= 33 && c <= 126 && c != '=') || ((c == ' ' || c == '\t') && !last)) {
                token.append(char(c));
            } else {
                token.append('=');
                token.append(kHex[c >> 4]);
                token.append(kHex[c & 0x0f]);
            }
            if (width + token.size() > kMaxLine) {
                out += "=\r\n";
                width = 0;
            }
            out += token;
            width += token.size();
        }
        if (n + 1 < lines.size())
            out += "\r\n";
    }
    return out;
}

QByteArray SmtpClient::dotStuff(const QByteArray& data)
{
    QByteArray out;
    out.reserve(data.size() + 16);
    bool lineStart = true;
    for (const char c : data) {
        if (lineStart && c == '.')
            out += '.';
        out += c;
        lineStart = c == '\n';
    }
    return out;
}

}  // namespace nutty
