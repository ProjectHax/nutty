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
// RSA, since Qt's Secure Transport backend (macOS) can only serve TLS with an
// RSA key.
constexpr auto kTestCert = R"(-----BEGIN CERTIFICATE-----
MIIDDTCCAfWgAwIBAgIUWNA+g0TP43IfXxwkGe2D/lSKamkwDQYJKoZIhvcNAQEL
BQAwFTETMBEGA1UEAwwKbnV0dHktdGVzdDAgFw0yNjEwMjkwMzEzNDVaGA8yMTI2
MTAwNTAzMTM0NVowFTETMBEGA1UEAwwKbnV0dHktdGVzdDCCASIwDQYJKoZIhvcN
AQEBBQADggEPADCCAQoCggEBAJSeyzCTa8XMkq0cFMZ13n1LQeUU7o3SwQsHxXBQ
3NwbdVYtIt9IRL4q6kLQMq5HcZslmAcxcCza8OHCE7fvfDNAQ26sAaZusUz32EaB
DOrjd/q2Cqu1UXplY1P81BAxH2zwhvWPXSEWqgou9C7VgamV5vcTFoOcbV1ihM5l
cG1jA6nDOYMbfo5znJDK0nX04g/c/09MivFKKZL6IoGQCMYLDAo8qH9ykNUXpjl0
AyOKYN8O2jmifz04IVcuO8Dr/rN+4ywkqPkfqyEM5gI4D9/LoclU9cihJXs8kMT/
ia2g+eatmSRRC1cBB8qPxa6fMoyg8/HlijtrqtW9j4eg7RsCAwEAAaNTMFEwHQYD
VR0OBBYEFBLgUMKMP774mp3OZD45fDes5JSSMB8GA1UdIwQYMBaAFBLgUMKMP774
mp3OZD45fDes5JSSMA8GA1UdEwEB/wQFMAMBAf8wDQYJKoZIhvcNAQELBQADggEB
AGuKCQKziBIRSyFzLp+RlM0qmGpNcCHkPcO5Xzoq8F2wuxpyU9rowLe/09HIvfkq
iqVP4wRS6kbuRYSHxl6ldZBILt3djQbg8khZrXP78TfPjDbDgLB0XCKs+1KHw8Ag
YiiWCExzHd165PCsxSAjyu3EWmfC0al9B3sVS6t27YihBOzxXHPkv2cKR9PFPcOm
L9UDNE97P6k+LWNuMTSL9wlW8ae//vuAeXQ4QtGTVm1pM3c+3Hki7q1Dcn7lSlQi
2xUASfrRwE3fGoRlfE5Zw4df9M1c14NSJsHvX7YWZfViKHZvHY6ymk9p4vnOf8Ow
moVGiJGI72e8TxZ5jhoLYJE=
-----END CERTIFICATE-----
)";
constexpr auto kTestKey = R"(-----BEGIN PRIVATE KEY-----
MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQCUnsswk2vFzJKt
HBTGdd59S0HlFO6N0sELB8VwUNzcG3VWLSLfSES+KupC0DKuR3GbJZgHMXAs2vDh
whO373wzQENurAGmbrFM99hGgQzq43f6tgqrtVF6ZWNT/NQQMR9s8Ib1j10hFqoK
LvQu1YGpleb3ExaDnG1dYoTOZXBtYwOpwzmDG36Oc5yQytJ19OIP3P9PTIrxSimS
+iKBkAjGCwwKPKh/cpDVF6Y5dAMjimDfDto5on89OCFXLjvA6/6zfuMsJKj5H6sh
DOYCOA/fy6HJVPXIoSV7PJDE/4mtoPnmrZkkUQtXAQfKj8WunzKMoPPx5Yo7a6rV
vY+HoO0bAgMBAAECggEAAS4yb1QoEhPEQth2zLBh5JGa0mJxc1ruGN9sM66K7Lsu
vQs304IxE8U+CaUXhN7bT5opYsmapfIDm8paS+yflexDefFhRCg+vbqsal6VIVHx
NdbxxmO8qcHZ25NI4ndP9vONCoJGpAAYcsBCLbaB4bDzTIIBwoAJk28JzTBtE0Wz
gaOtI2ECEZur1id5/kh6AlrA4l2QImo423+HUeH+rgPmCPeGjnrqi3gVYlSkviyp
kn3mlGxbSHIN5EFR5YaAWU8sj7nv7Nn0bfNdHmPdtPH0nG0tDjWlL2u7FaPou8X6
vhVwnkML2hPfc6HvY92dfLALY7cjaOAmzCNmijLqUQKBgQDKGIxuiChvXi+aOmr+
OozBXEkc+/LlTuIAKWZykNniUGXtIpy5KAEhxSz41UEgx9JUoegd5T7/dECmsfGG
MTc4qgxD7Rq/QPKYy+XHFkWLdKrevB98G1IfO/e8Vpl73No5uXKtNUk84V9XRf/r
l7lHqgZlDxxfl6sC7eAYm1EXzQKBgQC8QtcAvLuf1HkgpWYa5micIobBrLnK17Pl
3I5HE2Jyozs6BP2cF4BPOB0L4Ud1Z3TuV7LzVyWth5JFvMipQNLhIkWen+ZoNsJ7
n2GFPvptfI0spMkWugTWgsZqEQGLbF5Tl/5PPlRKjbsMDjUgpdP+rRg9dLj3IPkP
TsaZGTLghwKBgBg8F2MN2uN4BPdWuAvik4yAd5gNMpSDACEK1ek5K5ezEnX8Z5Tg
kjotffYdBKFctQFHJSGoowRZOYOD5TW1YyzdX3kpzsJOlIj0MorMReyyQedR4eit
Eg0C0PN54w/ybQztCy5wLGcrk5ZQo57p4+MPJZMq27ku4+gHBI+O2IulAoGBAKin
HGOdsFwMBYNYiTM6DKcrSxvAOCVevdwDjTZhSEW4s7tTcztd1mWhuGvHIrQrvZYz
3K2g+bEOjdyJth3iY8iqYkIrr0bXH72N19h9xe1Fslc3PKpudR0SxNuKZp7Xs10i
AgJNrUeo1tOC+tdHl7ZErT2JDpbd5CFxifrnlyuLAoGBAJpoNrIjaUlhaTZ6HUd+
GS0OgG0McYs45l4QWzSjMpvH2N0al/EflFcgcut3a3xoRmajDzZiVkGIzngril+Y
vDKHYbh+Zul/BjMICsoXk3pe/GaBxgYTj6mceG9HorwMXE/yGb/qgbYXlKhHVTcx
6NQu8p69M0vBWPusPJ28GtqX
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
        server.key = QSslKey(QByteArray(kTestKey), QSsl::Rsa, QSsl::Pem);
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
