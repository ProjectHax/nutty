// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/notify/EmailNotifier.h"
#include "core/notify/NotifyUtil.h"
#include "core/notify/SmtpClient.h"

#include <QSignalSpy>
#include <QSslSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimeZone>

#include <memory>

using namespace nutty;

namespace {

// A scripted SMTP server on 127.0.0.1. It never speaks TLS: STARTTLS is
// either not advertised or refused, which is what the client must handle.
class FakeSmtpServer : public QObject
{
public:
    QStringList capabilities { QStringLiteral("8BITMIME"), QStringLiteral("AUTH PLAIN LOGIN") };
    bool greet = true;
    bool ehlo = true;  // false: answer EHLO with 502 like an RFC 821 server
    int authReply = 235;
    QByteArray rejectRecipient;  // RCPT TO for this address gets 550

    // Everything each session received, in order.
    struct Session {
        QList<QByteArray> commands;
        QByteArray data;  // between DATA and the final ".", still dot-stuffed
    };
    QList<Session> sessions;

    FakeSmtpServer()
    {
        m_server.listen(QHostAddress::LocalHost, 0);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (auto* socket = m_server.nextPendingConnection())
                serve(socket);
        });
    }

    quint16 port() const { return m_server.serverPort(); }

    QList<QByteArray> allCommands() const
    {
        QList<QByteArray> out;
        for (const auto& session : sessions)
            out += session.commands;
        return out;
    }

private:
    void serve(QTcpSocket* socket)
    {
        sessions.append(Session());
        const auto index = sessions.size() - 1;
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        auto buffer = std::make_shared<QByteArray>();
        auto inData = std::make_shared<bool>(false);
        auto authLogin = std::make_shared<int>(0);
        connect(socket, &QTcpSocket::readyRead, this, [=, this] {
            buffer->append(socket->readAll());
            auto& session = sessions[index];
            while (true) {
                if (*inData) {
                    const auto end = buffer->indexOf("\r\n.\r\n");
                    if (end < 0)
                        return;
                    session.data = buffer->left(end + 2);
                    buffer->remove(0, end + 5);
                    *inData = false;
                    socket->write("250 2.0.0 queued\r\n");
                    continue;
                }
                const auto eol = buffer->indexOf("\r\n");
                if (eol < 0)
                    return;
                const auto line = buffer->left(eol);
                buffer->remove(0, eol + 2);
                session.commands << line;
                socket->write(reply(line, inData.get(), authLogin.get()));
                if (line == "QUIT")
                    socket->disconnectFromHost();
            }
        });
        if (greet)
            socket->write("220 fake.test ESMTP\r\n");
    }

    QByteArray reply(const QByteArray& line, bool* inData, int* authLogin)
    {
        if (*authLogin == 1) {
            *authLogin = 2;
            return "334 UGFzc3dvcmQ6\r\n";
        }
        if (*authLogin == 2) {
            *authLogin = 0;
            return QByteArray::number(authReply) + " auth\r\n";
        }
        const auto verb = line.left(line.indexOf(' ') < 0 ? line.size() : line.indexOf(' ')).toUpper();
        if (verb == "EHLO" && !ehlo)
            return "502 5.5.1 unknown\r\n";
        if (verb == "EHLO") {
            QByteArray out = "250";
            out += capabilities.isEmpty() ? " " : "-";
            out += "fake.test\r\n";
            for (qsizetype i = 0; i < capabilities.size(); ++i)
                out += (i + 1 == capabilities.size() ? "250 " : "250-") + capabilities.at(i).toLatin1()
                    + "\r\n";
            return out;
        }
        if (verb == "HELO")
            return "250 fake.test\r\n";
        if (verb == "STARTTLS")
            return "454 4.7.0 TLS not available\r\n";
        if (line.startsWith("AUTH PLAIN"))
            return QByteArray::number(authReply) + " auth\r\n";
        if (line == "AUTH LOGIN") {
            *authLogin = 1;
            return "334 VXNlcm5hbWU6\r\n";
        }
        if (verb == "MAIL")
            return "250 2.1.0 ok\r\n";
        if (verb == "RCPT") {
            if (!rejectRecipient.isEmpty() && line.contains(rejectRecipient))
                return "550 5.1.1 no such user\r\n";
            return "250 2.1.5 ok\r\n";
        }
        if (verb == "DATA") {
            *inData = true;
            return "354 go ahead\r\n";
        }
        if (verb == "QUIT")
            return "221 bye\r\n";
        return "502 unknown\r\n";
    }

    QTcpServer m_server;
};

SmtpClient::Settings plainSettings(quint16 port)
{
    SmtpClient::Settings settings;
    settings.host = QStringLiteral("127.0.0.1");
    settings.port = port;
    settings.security = SmtpClient::Security::None;
    settings.timeoutMs = 5000;
    return settings;
}

SmtpClient::Message sampleMessage()
{
    SmtpClient::Message message;
    message.from = QStringLiteral("nutty <alerts@example.com>");
    message.to = { QStringLiteral("ops@example.com"), QStringLiteral("Second <second@example.org>") };
    message.subject = QStringLiteral("On battery");
    message.body = QStringLiteral("UPS is on battery.\n.hidden line\nend");
    message.date = QDateTime(QDate(2026, 3, 4), QTime(5, 6, 7), QTimeZone::utc());
    return message;
}

// Runs one send and returns (ok, error).
std::pair<bool, QString> runSend(const SmtpClient::Settings& settings, const SmtpClient::Message& message)
{
    SmtpClient client;
    QSignalSpy spy(&client, &SmtpClient::finished);
    client.send(settings, message);
    // Configuration errors are reported from inside send().
    if (spy.isEmpty() && !spy.wait(10000))
        return { false, QStringLiteral("no finished() signal") };
    return { spy.at(0).at(0).toBool(), spy.at(0).at(1).toString() };
}

}  // namespace

class TestSmtp : public QObject
{
    Q_OBJECT

private slots:
    void plainDelivery()
    {
        FakeSmtpServer server;
        server.capabilities = { QStringLiteral("8BITMIME") };
        // Kept alive past finished() so the courtesy QUIT goes out.
        SmtpClient client;
        QSignalSpy finished(&client, &SmtpClient::finished);
        client.send(plainSettings(server.port()), sampleMessage());
        QVERIFY(finished.wait(10000));
        QVERIFY2(finished.at(0).at(0).toBool(), qPrintable(finished.at(0).at(1).toString()));
        QTRY_VERIFY(server.allCommands().contains("QUIT"));

        const auto commands = server.allCommands();
        QCOMPARE(commands.value(0), QByteArray("EHLO localhost"));
        QCOMPARE(commands.value(1), QByteArray("MAIL FROM:<alerts@example.com>"));
        QCOMPARE(commands.value(2), QByteArray("RCPT TO:<ops@example.com>"));
        QCOMPARE(commands.value(3), QByteArray("RCPT TO:<second@example.org>"));
        QCOMPARE(commands.value(4), QByteArray("DATA"));
        for (const auto& command : commands)
            QVERIFY(!command.startsWith("AUTH"));

        const auto data = server.sessions.value(0).data;
        QVERIFY(data.contains("Date: Wed, 04 Mar 2026 05:06:07 +0000\r\n"));
        QVERIFY(data.contains("Subject: On battery\r\n"));
        QVERIFY(data.contains("Content-Type: text/plain; charset=UTF-8\r\n"));
        QVERIFY(data.contains("Content-Transfer-Encoding: quoted-printable\r\n"));
        QVERIFY(data.contains("Message-ID: <"));
        QVERIFY(data.contains("@nutty.invalid>\r\n"));
        QVERIFY(data.contains("To: ops@example.com, Second <second@example.org>\r\n"));
        // Dot-stuffing on the wire.
        QVERIFY(data.contains("\r\n..hidden line\r\n"));
    }

    void heloFallback()
    {
        FakeSmtpServer server;
        server.ehlo = false;
        SmtpClient::Settings settings = plainSettings(server.port());
        settings.heloName = QStringLiteral("client.test");
        const auto [ok, error] = runSend(settings, sampleMessage());
        QVERIFY2(ok, qPrintable(error));
        QCOMPARE(server.allCommands().value(0), QByteArray("EHLO client.test"));
        QCOMPARE(server.allCommands().value(1), QByteArray("HELO client.test"));
        QCOMPARE(server.allCommands().value(2), QByteArray("MAIL FROM:<alerts@example.com>"));
    }

    void authPlain()
    {
        FakeSmtpServer server;
        auto settings = plainSettings(server.port());
        settings.username = QStringLiteral("user@example.com");
        settings.password = QStringLiteral("pässword");
        const auto [ok, error] = runSend(settings, sampleMessage());
        QVERIFY2(ok, qPrintable(error));

        QByteArray expected(1, '\0');
        expected += "user@example.com";
        expected += '\0';
        expected += QStringLiteral("pässword").toUtf8();
        QCOMPARE(server.allCommands().value(1), "AUTH PLAIN " + expected.toBase64());
        QCOMPARE(server.allCommands().value(2), QByteArray("MAIL FROM:<alerts@example.com>"));
    }

    void authLogin()
    {
        FakeSmtpServer server;
        server.capabilities = { QStringLiteral("AUTH LOGIN") };
        auto settings = plainSettings(server.port());
        settings.username = QStringLiteral("user");
        settings.password = QStringLiteral("secret");
        const auto [ok, error] = runSend(settings, sampleMessage());
        QVERIFY2(ok, qPrintable(error));
        const auto commands = server.allCommands();
        QCOMPARE(commands.value(1), QByteArray("AUTH LOGIN"));
        QCOMPARE(commands.value(2), QByteArray("user").toBase64());
        QCOMPARE(commands.value(3), QByteArray("secret").toBase64());
    }

    void authRejected()
    {
        FakeSmtpServer server;
        server.authReply = 535;
        auto settings = plainSettings(server.port());
        settings.username = QStringLiteral("user");
        settings.password = QStringLiteral("wrong");
        const auto [ok, error] = runSend(settings, sampleMessage());
        QVERIFY(!ok);
        QVERIFY2(error.contains(QStringLiteral("535")), qPrintable(error));
        for (const auto& command : server.allCommands())
            QVERIFY(!command.startsWith("MAIL"));
    }

    void noSupportedAuthMechanism()
    {
        FakeSmtpServer server;
        server.capabilities = { QStringLiteral("AUTH CRAM-MD5") };
        auto settings = plainSettings(server.port());
        settings.username = QStringLiteral("user");
        settings.password = QStringLiteral("secret");
        const auto [ok, error] = runSend(settings, sampleMessage());
        QVERIFY(!ok);
        QVERIFY(error.contains(QStringLiteral("PLAIN")));
    }

    void startTlsNotOffered()
    {
        if (!QSslSocket::supportsSsl())
            QSKIP("No TLS backend");
        FakeSmtpServer server;
        auto settings = plainSettings(server.port());
        settings.security = SmtpClient::Security::StartTls;
        settings.username = QStringLiteral("user");
        settings.password = QStringLiteral("secret");
        const auto [ok, error] = runSend(settings, sampleMessage());
        QVERIFY(!ok);
        QVERIFY2(error.contains(QStringLiteral("STARTTLS")), qPrintable(error));
        // Credentials never go out in the clear.
        for (const auto& command : server.allCommands())
            QVERIFY(!command.startsWith("AUTH"));
    }

    void startTlsRefused()
    {
        if (!QSslSocket::supportsSsl())
            QSKIP("No TLS backend");
        FakeSmtpServer server;
        server.capabilities = { QStringLiteral("STARTTLS"), QStringLiteral("AUTH PLAIN") };
        auto settings = plainSettings(server.port());
        settings.security = SmtpClient::Security::StartTls;
        settings.username = QStringLiteral("user");
        settings.password = QStringLiteral("secret");
        const auto [ok, error] = runSend(settings, sampleMessage());
        QVERIFY(!ok);
        QVERIFY2(error.contains(QStringLiteral("454")), qPrintable(error));
        for (const auto& command : server.allCommands())
            QVERIFY(!command.startsWith("AUTH"));
    }

    void recipientRejected()
    {
        FakeSmtpServer server;
        server.rejectRecipient = "second@example.org";
        const auto [ok, error] = runSend(plainSettings(server.port()), sampleMessage());
        QVERIFY(!ok);
        QVERIFY2(error.contains(QStringLiteral("550")), qPrintable(error));
        for (const auto& command : server.allCommands())
            QVERIFY(command != "DATA");
    }

    void timeout()
    {
        FakeSmtpServer server;
        server.greet = false;
        auto settings = plainSettings(server.port());
        settings.timeoutMs = 300;
        const auto [ok, error] = runSend(settings, sampleMessage());
        QVERIFY(!ok);
        QVERIFY(!error.isEmpty());
    }

    void connectionRefused()
    {
        quint16 port = 0;
        {
            QTcpServer probe;
            probe.listen(QHostAddress::LocalHost, 0);
            port = probe.serverPort();
        }
        const auto [ok, error] = runSend(plainSettings(port), sampleMessage());
        QVERIFY(!ok);
        QVERIFY(!error.isEmpty());
    }

    void missingConfiguration()
    {
        auto settings = plainSettings(25);
        settings.host.clear();
        QVERIFY(!runSend(settings, sampleMessage()).first);

        auto message = sampleMessage();
        message.to.clear();
        QVERIFY(!runSend(plainSettings(25), message).first);
    }

    void subjectEncoding()
    {
        SmtpClient::Message message = sampleMessage();
        message.subject = QStringLiteral("Batterie schwach – 15 %");
        const auto data = SmtpClient::buildMessage(message);
        const auto subjectStart = data.indexOf("Subject: ");
        QVERIFY(subjectStart >= 0);
        const auto subject = data.mid(subjectStart, data.indexOf("\r\nMessage-ID") - subjectStart);
        QVERIFY2(subject.startsWith("Subject: =?UTF-8?B?"), subject.constData());
        // Every byte of the header is 7-bit.
        for (const char c : subject)
            QVERIFY(uchar(c) < 0x80);

        // Decode the encoded words back.
        QByteArray decoded;
        for (const auto& word : subject.mid(9).replace("\r\n ", " ").split(' ')) {
            QVERIFY(word.startsWith("=?UTF-8?B?") && word.endsWith("?="));
            decoded += QByteArray::fromBase64(word.mid(10, word.size() - 12));
        }
        QCOMPARE(QString::fromUtf8(decoded), message.subject);
    }

    void longSubjectFolds()
    {
        const auto text = QString(60, QChar(0x00e9));  // 120 bytes of UTF-8
        const auto encoded = notify::encodeHeaderWord(text);
        QVERIFY(encoded.contains("\r\n "));
        for (const auto& line : encoded.split('\n'))
            QVERIFY(line.size() <= 76);
        QCOMPARE(notify::encodeHeaderWord(QStringLiteral("plain ascii")), QByteArray("plain ascii"));
    }

    void quotedPrintable()
    {
        QCOMPARE(SmtpClient::quotedPrintable("a=b"), QByteArray("a=3Db"));
        QCOMPARE(SmtpClient::quotedPrintable(QStringLiteral("é").toUtf8()), QByteArray("=C3=A9"));
        QCOMPARE(SmtpClient::quotedPrintable("trailing \nnext"), QByteArray("trailing=20\r\nnext"));
        QCOMPARE(SmtpClient::quotedPrintable("a\r\nb"), QByteArray("a\r\nb"));

        const auto wrapped = SmtpClient::quotedPrintable(QByteArray(200, 'x'));
        for (const auto& line : wrapped.split('\n'))
            QVERIFY(line.trimmed().size() <= 76);
        QCOMPARE(QByteArray(wrapped).replace("=\r\n", ""), QByteArray(200, 'x'));
    }

    void dotStuffing()
    {
        QCOMPARE(SmtpClient::dotStuff(".start\r\nmid.dle\r\n.\r\n..x"),
            QByteArray("..start\r\nmid.dle\r\n..\r\n...x"));
        QCOMPARE(SmtpClient::dotStuff("none"), QByteArray("none"));
    }

    void extractAddress()
    {
        QCOMPARE(SmtpClient::extractAddress(QStringLiteral("Name <a@b.c>")), QStringLiteral("a@b.c"));
        QCOMPARE(SmtpClient::extractAddress(QStringLiteral("  a@b.c ")), QStringLiteral("a@b.c"));
    }

    void emailNotifierQueuesEvents()
    {
        FakeSmtpServer server;
        server.capabilities = {};
        ChannelConfig channel;
        channel.id = QUuid::createUuid();
        channel.type = ChannelType::Email;
        channel.settings = {
            { QString::fromLatin1(keys::EmailHost), QStringLiteral("127.0.0.1") },
            { QString::fromLatin1(keys::EmailPort), int(server.port()) },
            { QString::fromLatin1(keys::EmailSecurity), QStringLiteral("none") },
            { QString::fromLatin1(keys::EmailFrom), QStringLiteral("alerts@example.com") },
            { QString::fromLatin1(keys::EmailTo), QStringLiteral("a@example.com; b@example.com") },
        };
        EmailNotifier notifier(channel);
        QSignalSpy sent(&notifier, &Notifier::sent);
        QSignalSpy failed(&notifier, &Notifier::failed);

        Event first;
        first.type = EventType::OnBattery;
        first.title = QStringLiteral("On battery");
        first.message = QStringLiteral("first");
        Event second = first;
        second.title = QStringLiteral("Power restored");
        second.message = QStringLiteral("second");
        notifier.send(first);
        notifier.send(second);

        QTRY_COMPARE_WITH_TIMEOUT(sent.size(), 2, 10000);
        QCOMPARE(failed.size(), 0);
        QCOMPARE(server.sessions.size(), 2);
        QVERIFY(server.sessions.at(0).data.contains("Subject: [nutty] On battery"));
        QVERIFY(server.sessions.at(1).data.contains("Subject: [nutty] Power restored"));
        QVERIFY(server.sessions.at(0).commands.contains("RCPT TO:<b@example.com>"));
    }
};

QTEST_GUILESS_MAIN(TestSmtp)
#include "tst_smtp.moc"
