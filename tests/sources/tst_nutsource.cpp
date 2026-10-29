// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/sources/nut/NutProtocol.h"
#include "core/sources/nut/NutSource.h"

#include <QCryptographicHash>
#include <QPointer>
#include <QSignalSpy>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpServer>
#include <QTest>
#include <QUuid>

#include <algorithm>

using namespace nutty;

namespace {

// A throwaway self-signed certificate (CN=nutty-test), only for these tests.
constexpr auto kTestCert = R"(-----BEGIN CERTIFICATE-----
MIIBgTCCASegAwIBAgIUUY2M1e8CmfsCwjsETFaEjmjoyAIwCgYIKoZIzj0EAwIw
FTETMBEGA1UEAwwKbnV0dHktdGVzdDAgFw0yNjEwMjgwMjU2MTVaGA8yMTI2MTAw
NDAyNTYxNVowFTETMBEGA1UEAwwKbnV0dHktdGVzdDBZMBMGByqGSM49AgEGCCqG
SM49AwEHA0IABJLqMXK5ZJ16eZ6qmXje72nAp/L/tvouYn5TUD9qsJWLIvm025sF
dMVSliJ7+9q+zW09/O4+V1Bl4bSdcTvkYGqjUzBRMB0GA1UdDgQWBBRzhkl5FEBC
j7Wu2sxb8Vkr2chXyTAfBgNVHSMEGDAWgBRzhkl5FEBCj7Wu2sxb8Vkr2chXyTAP
BgNVHRMBAf8EBTADAQH/MAoGCCqGSM49BAMCA0gAMEUCID3GJ3HpfVWJ+6t6D7uH
4X80fT7D0Gz/p47W2i2mMRK3AiEAsP74uQmIVP+getSkU86U+zJbbo+cPYH+bE5v
HM8HAtg=
-----END CERTIFICATE-----
)";
constexpr auto kTestKey = R"(-----BEGIN PRIVATE KEY-----
MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgDkT+KHFHkWqF0One
JSlVQxG7fDHVaf7d0lpdaFPpeR6hRANCAASS6jFyuWSdenmeqpl43u9pwKfy/7b6
LmJ+U1A/arCViyL5tNubBXTFUpYie/vavs1tPfzuPldQZeG0nXE75GBq
-----END PRIVATE KEY-----
)";

// Speaks enough of upsd's protocol for NutSource and NutDiscovery.
class FakeUpsd : public QTcpServer
{
public:
    explicit FakeUpsd(QObject* parent = nullptr)
        : QTcpServer(parent)
    {
        listen(QHostAddress::LocalHost, 0);
    }

    QMap<QString, QString> vars {
        { "ups.status", "OL" },
        { "battery.charge", "100" },
        { "battery.runtime", "1800" },
        { "device.model", "Test UPS" },
    };
    QStringList upsNames { "ups" };
    QString password;  // required when set
    QString listVarError;  // LIST VAR answers ERR <this> when set
    bool tls = false;  // STARTTLS supported
    QSslCertificate certificate;
    QSslKey key;

    QStringList lines;  // every command received, PASSWORD values masked
    QStringList rawLines;  // every command received, verbatim
    int connections = 0;

    bool received(const QString& prefix) const
    {
        return std::any_of(
            lines.cbegin(), lines.cend(), [&](const QString& l) { return l.startsWith(prefix); });
    }

    void dropClients()
    {
        for (const auto& c : std::as_const(m_clients)) {
            if (c)
                c->abort();
        }
    }

protected:
    void incomingConnection(qintptr descriptor) override
    {
        auto* socket = new QSslSocket(this);
        if (!socket->setSocketDescriptor(descriptor)) {
            delete socket;
            return;
        }
        ++connections;
        m_clients << socket;
        if (tls) {
            socket->setLocalCertificate(certificate);
            socket->setPrivateKey(key);
        }
        connect(socket, &QSslSocket::readyRead, this, [this, socket] {
            while (socket->canReadLine())
                handle(socket, QString::fromUtf8(socket->readLine()).trimmed());
        });
        connect(socket, &QSslSocket::disconnected, socket, &QObject::deleteLater);
    }

private:
    void handle(QSslSocket* socket, const QString& line)
    {
        rawLines << line;
        const QStringList w = nut::tokenize(line);
        lines << (w.value(0) == u"PASSWORD" ? QStringLiteral("PASSWORD ***") : line);
        const QString cmd = w.value(0);
        const auto send = [socket](const QString& text) { socket->write(text.toUtf8()); };

        if (cmd == u"STARTTLS") {
            if (!tls) {
                send("ERR FEATURE-NOT-CONFIGURED\n");
                return;
            }
            send("OK STARTTLS\n");
            socket->flush();
            socket->startServerEncryption();
        } else if (cmd == u"USERNAME") {
            send("OK\n");
        } else if (cmd == u"PASSWORD") {
            send(!password.isEmpty() && w.value(1) != password ? "ERR ACCESS-DENIED\n" : "OK\n");
        } else if (cmd == u"LOGIN") {
            send(upsNames.contains(w.value(1)) ? "OK\n" : "ERR UNKNOWN-UPS\n");
        } else if (cmd == u"LOGOUT") {
            send("OK Goodbye\n");
            socket->disconnectFromHost();
        } else if (cmd == u"LIST" && w.value(1) == u"UPS") {
            QString out = "BEGIN LIST UPS\n";
            for (const auto& n : std::as_const(upsNames))
                out += QStringLiteral("UPS %1 \"Description of %1\"\n").arg(n);
            send(out + "END LIST UPS\n");
        } else if (cmd == u"LIST" && w.value(1) == u"VAR") {
            const QString ups = w.value(2);
            if (!upsNames.contains(ups)) {
                send("ERR UNKNOWN-UPS\n");
                return;
            }
            if (!listVarError.isEmpty()) {
                send("ERR " + listVarError + "\n");
                return;
            }
            QString out = "BEGIN LIST VAR " + ups + "\n";
            for (auto it = vars.cbegin(); it != vars.cend(); ++it)
                out += QStringLiteral("VAR %1 %2 %3\n").arg(ups, it.key(), nut::quote(it.value()));
            send(out + "END LIST VAR " + ups + "\n");
        } else {
            send("ERR UNKNOWN-COMMAND\n");
        }
    }

    QList<QPointer<QSslSocket>> m_clients;
};

DeviceConfig nutConfig(const FakeUpsd& server, QVariantMap extra = {}, QVariantMap secrets = {})
{
    DeviceConfig c;
    c.id = QUuid::createUuid();
    c.type = SourceType::Nut;
    c.pollIntervalSec = 60;  // the tests poll explicitly
    c.settings = {
        { keys::NutHost, QStringLiteral("127.0.0.1") },
        { keys::NutPort, server.serverPort() },
        { keys::NutUps, QStringLiteral("ups") },
    };
    for (auto it = extra.cbegin(); it != extra.cend(); ++it)
        c.settings.insert(it.key(), it.value());
    c.secrets = std::move(secrets);
    return c;
}

}  // namespace

class TestNutSource : public QObject
{
    Q_OBJECT

private slots:
    void pollsAndPublishes()
    {
        FakeUpsd server;
        NutSource source(nutConfig(server));
        QSignalSpy status(&source, &Source::statusUpdated);
        source.start();
        QVERIFY(status.wait(5000));

        const DeviceStatus s = source.lastStatus();
        QVERIFY(s.online());
        QCOMPARE(*s.charge, 100.0);
        QCOMPARE(*s.runtime, 1800);
        QCOMPARE(s.model, QStringLiteral("Test UPS"));
        QCOMPARE(source.connectionState(), ConnectionState::Connected);
        QVERIFY(server.received("LIST VAR ups"));
        QVERIFY(!server.received("USERNAME"));  // no credentials configured

        server.vars["ups.status"] = "OB DISCHRG";
        server.vars["battery.charge"] = "80";
        source.pollNow();
        QVERIFY(status.wait(5000));
        QVERIFY(source.lastStatus().onBattery());
        QCOMPARE(*source.lastStatus().charge, 80.0);
        QCOMPARE(server.connections, 1);  // one persistent connection
        QCOMPARE(source.describe(), QStringLiteral("ups@127.0.0.1:%1").arg(server.serverPort()));
    }

    void authenticates()
    {
        FakeUpsd server;
        server.password = QStringLiteral("s3cret \"pass\"");
        NutSource source(nutConfig(
            server, { { keys::NutUsername, "monuser" } }, { { keys::NutPassword, server.password } }));
        QSignalSpy status(&source, &Source::statusUpdated);
        source.start();
        QVERIFY(status.wait(5000));
        QVERIFY(server.received("USERNAME monuser"));
        QVERIFY(server.rawLines.contains(
            QString::fromUtf8(nut::command({ "PASSWORD", server.password })).trimmed()));
    }

    void wrongPasswordIsAuthError()
    {
        FakeUpsd server;
        server.password = QStringLiteral("right");
        NutSource source(
            nutConfig(server, { { keys::NutUsername, "monuser" } }, { { keys::NutPassword, "wrong" } }));
        QSignalSpy errors(&source, &Source::errorOccurred);
        source.start();
        QVERIFY(errors.wait(5000));
        QCOMPARE(errors.first().at(0).value<SourceError>(), SourceError::Auth);
        QVERIFY(!server.received("LIST VAR"));
        source.stop();
    }

    void staleDataKeepsConnection()
    {
        FakeUpsd server;
        server.listVarError = QStringLiteral("DATA-STALE");
        NutSource source(nutConfig(server));
        QSignalSpy errors(&source, &Source::errorOccurred);
        QSignalSpy status(&source, &Source::statusUpdated);
        source.start();
        QVERIFY(errors.wait(5000));
        QCOMPARE(errors.first().at(0).value<SourceError>(), SourceError::DataStale);
        QCOMPARE(source.connectionState(), ConnectionState::Error);

        server.listVarError.clear();
        source.pollNow();
        QVERIFY(status.wait(5000));
        QCOMPARE(source.connectionState(), ConnectionState::Connected);
        QCOMPARE(server.connections, 1);
    }

    void unknownUpsIsNotFound()
    {
        FakeUpsd server;
        server.upsNames = { "other" };
        NutSource source(nutConfig(server));
        QSignalSpy errors(&source, &Source::errorOccurred);
        source.start();
        QVERIFY(errors.wait(5000));
        QCOMPARE(errors.first().at(0).value<SourceError>(), SourceError::NotFound);
    }

    void missingUpsNameIsConfigError()
    {
        FakeUpsd server;
        DeviceConfig c = nutConfig(server);
        c.settings.remove(keys::NutUps);
        NutSource source(c);
        QSignalSpy errors(&source, &Source::errorOccurred);
        source.start();
        QCOMPARE(errors.size(), 1);
        QCOMPARE(errors.first().at(0).value<SourceError>(), SourceError::Config);
        QCOMPARE(server.connections, 0);
    }

    void secondaryLogsInAndOutWithoutReconnecting()
    {
        FakeUpsd server;
        NutSource source(nutConfig(server, { { keys::NutSecondary, true } }));
        QSignalSpy status(&source, &Source::statusUpdated);
        source.start();
        QVERIFY(status.wait(5000));
        QVERIFY(server.received("LOGIN ups"));
        QVERIFY(source.isLoggedIn());

        source.prepareForShutdown();
        QTRY_VERIFY_WITH_TIMEOUT(server.received("LOGOUT"), 5000);
        QVERIFY(!source.isLoggedIn());

        // The server hangs up after LOGOUT; a shutting-down secondary must not
        // log in again (the primary would wait for it once more).
        QTest::qWait(NutSource::kBackoffMs[0] + 500);
        QCOMPARE(server.connections, 1);
        QCOMPARE(std::count(server.lines.cbegin(), server.lines.cend(), QStringLiteral("LOGIN ups")), 1);
    }

    void reconnectsAfterDrop()
    {
        FakeUpsd server;
        NutSource source(nutConfig(server));
        QSignalSpy status(&source, &Source::statusUpdated);
        QSignalSpy errors(&source, &Source::errorOccurred);
        source.start();
        QVERIFY(status.wait(5000));

        server.dropClients();
        QVERIFY(errors.wait(5000));
        QCOMPARE(errors.last().at(0).value<SourceError>(), SourceError::Network);
        QVERIFY(status.wait(NutSource::kBackoffMs[0] + 5000));
        QCOMPARE(server.connections, 2);
    }

    void stopDisconnects()
    {
        FakeUpsd server;
        NutSource source(nutConfig(server));
        QSignalSpy status(&source, &Source::statusUpdated);
        source.start();
        QVERIFY(status.wait(5000));
        source.stop();
        QCOMPARE(source.connectionState(), ConnectionState::Disconnected);
        QTest::qWait(NutSource::kBackoffMs[0] + 300);
        QCOMPARE(server.connections, 1);
    }

    void requiredTlsRefusedSendsNoCredentials()
    {
        FakeUpsd server;  // no TLS
        NutSource source(nutConfig(server, { { keys::NutTls, "required" }, { keys::NutUsername, "monuser" } },
            { { keys::NutPassword, "secret" } }));
        QSignalSpy errors(&source, &Source::errorOccurred);
        source.start();
        QVERIFY(errors.wait(5000));
        QCOMPARE(errors.first().at(0).value<SourceError>(), SourceError::Tls);
        QCOMPARE(server.lines, QStringList { "STARTTLS" });
        source.stop();
    }

    void tlsWithPinnedCertificate_data()
    {
        QTest::addColumn<bool>("pinCorrect");
        QTest::newRow("matching pin") << true;
        QTest::newRow("wrong pin") << false;
    }

    void tlsWithPinnedCertificate()
    {
        QFETCH(bool, pinCorrect);
        if (!QSslSocket::supportsSsl())
            QSKIP("No TLS backend");
        FakeUpsd server;
        server.tls = true;
        server.certificate = QSslCertificate(QByteArray(kTestCert), QSsl::Pem);
        server.key = QSslKey(QByteArray(kTestKey), QSsl::Ec, QSsl::Pem);
        if (server.certificate.isNull() || server.key.isNull())
            QSKIP("The TLS backend can't load the test certificate");

        QString pin = QString::fromLatin1(server.certificate.digest(QCryptographicHash::Sha256).toHex(':'));
        if (!pinCorrect)
            pin = QString(64, u'a');

        NutSource source(nutConfig(server,
            { { keys::NutTls, "required" }, { keys::NutFingerprint, pin }, { keys::NutUsername, "monuser" } },
            { { keys::NutPassword, "secret" } }));
        QSignalSpy status(&source, &Source::statusUpdated);
        QSignalSpy errors(&source, &Source::errorOccurred);
        source.start();
        if (pinCorrect) {
            QVERIFY(status.wait(5000));
            QVERIFY(server.received("PASSWORD"));
        } else {
            QVERIFY(errors.wait(5000));
            QCOMPARE(errors.first().at(0).value<SourceError>(), SourceError::Tls);
            QVERIFY(!server.received("USERNAME"));
            QVERIFY(!server.received("PASSWORD"));
        }
        source.stop();
    }

    void discoveryListsUps()
    {
        FakeUpsd server;
        server.upsNames = { "ups", "rack" };
        QPointer<Discovery> discovery = new NutDiscovery(nutConfig(server).settings, {});
        QSignalSpy finished(discovery.data(), &Discovery::finished);
        QVERIFY(finished.wait(5000));
        const auto devices = finished.first().at(0).value<QList<DiscoveredDevice>>();
        QCOMPARE(devices.size(), 2);
        QCOMPARE(devices.at(0).id, QStringLiteral("ups"));
        QCOMPARE(devices.at(1).id, QStringLiteral("rack"));
        QCOMPARE(devices.at(1).description, QStringLiteral("Description of rack"));
        QTRY_VERIFY(discovery.isNull());  // deletes itself
    }

    void discoveryFailsWhenUnreachable()
    {
        quint16 port = 0;
        {
            FakeUpsd closed;
            port = closed.serverPort();
        }
        QPointer<Discovery> discovery
            = new NutDiscovery({ { keys::NutHost, "127.0.0.1" }, { keys::NutPort, port } }, {});
        QSignalSpy failed(discovery.data(), &Discovery::failed);
        QVERIFY(failed.wait(5000));
        QTRY_VERIFY(discovery.isNull());
    }
};

QTEST_GUILESS_MAIN(TestNutSource)
#include "tst_nutsource.moc"
