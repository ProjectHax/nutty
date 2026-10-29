// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/sources/apcupsd/ApcupsdProtocol.h"
#include "core/sources/apcupsd/ApcupsdSource.h"

#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUuid>

using namespace nutty;
using namespace nutty::apcupsd;

namespace {

const QStringList kOnlineReport {
    "APC      : 001,036,0868",
    "HOSTNAME : example",
    "MODEL    : Back-UPS RS 1500G ",
    "STATUS   : ONLINE ",
    "LINEV    : 121.0 Volts",
    "LOADPCT  : 20.0 Percent",
    "BCHARGE  : 100.0 Percent",
    "TIMELEFT : 45.5 Minutes",
    "MBATTCHG : 10 Percent",
    "MINTIMEL : 3 Minutes",
    "OUTPUTV  : 120.0 Volts",
    "BATTV    : 27.1 Volts",
    "NOMPOWER : 865 Watts",
    "SERIALNO : 3B0000X00000",
};

// apcupsd's NIS: reads the "status" request record and answers with one
// record per line plus the zero-length terminator.
class FakeNis : public QTcpServer
{
public:
    FakeNis()
    {
        listen(QHostAddress::LocalHost, 0);
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* socket = nextPendingConnection()) {
                ++connections;
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    RecordReader request;
                    request.append(socket->readAll());
                    if (request.records().value(0) != "status")
                        return;
                    if (silent)
                        return;
                    QByteArray out;
                    for (const QString& line : std::as_const(report))
                        out += encodeRecord((line + u'\n').toLatin1());
                    if (!truncate)
                        out += encodeRecord({});
                    socket->write(out);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

    QStringList report = kOnlineReport;
    bool silent = false;  // never answer
    bool truncate = false;  // hang up without the terminator
    int connections = 0;
};

DeviceConfig apcConfig(quint16 port)
{
    DeviceConfig c;
    c.id = QUuid::createUuid();
    c.type = SourceType::Apcupsd;
    c.pollIntervalSec = 60;
    c.settings = { { keys::ApcHost, "127.0.0.1" }, { keys::ApcPort, port } };
    return c;
}

QMap<QString, QString> parse(const QStringList& lines)
{
    QList<QByteArray> records;
    for (const auto& l : lines)
        records << (l + u'\n').toLatin1();
    return parseStatusRecords(records);
}

}  // namespace

class TestApcupsd : public QObject
{
    Q_OBJECT

private slots:
    void records()
    {
        QCOMPARE(encodeRecord("status"), QByteArray("\x00\x06status", 8));

        RecordReader reader;
        const QByteArray stream = encodeRecord("A : 1\n") + encodeRecord("B : 2\n") + encodeRecord({});
        // Feed byte by byte: records may arrive split anywhere.
        for (char c : stream) {
            QVERIFY(!reader.isComplete());
            reader.append(QByteArrayView(&c, 1));
        }
        QVERIFY(reader.isComplete());
        QCOMPARE(reader.records().size(), 2);
        QCOMPARE(reader.records().at(1), QByteArray("B : 2\n"));

        reader.clear();
        QVERIFY(!reader.isComplete());
        QVERIFY(reader.records().isEmpty());
    }

    void parsing()
    {
        const auto values = parse(kOnlineReport);
        QCOMPARE(values.value("STATUS"), QStringLiteral("ONLINE"));
        QCOMPARE(values.value("MODEL"), QStringLiteral("Back-UPS RS 1500G"));
        QCOMPARE(values.value("TIMELEFT"), QStringLiteral("45.5 Minutes"));
        QCOMPARE(leadingNumber("45.5 Minutes"), std::optional<double>(45.5));
        QCOMPARE(leadingNumber("-3 C"), std::optional<double>(-3));
        QVERIFY(!leadingNumber("N/A"));
    }

    void onlineStatus()
    {
        const auto result = statusFromValues(parse(kOnlineReport));
        const DeviceStatus& s = result.status;
        QVERIFY(!result.commLost);
        QVERIFY(s.online());
        QVERIFY(!s.has(StatusFlag::Charging));  // full
        QCOMPARE(*s.charge, 100.0);
        QCOMPARE(*s.runtime, 2730);
        QCOMPARE(*s.load, 20.0);
        QCOMPARE(*s.inputVoltage, 121.0);
        QCOMPARE(*s.outputVoltage, 120.0);
        QCOMPARE(*s.batteryVoltage, 27.1);
        QCOMPARE(*s.chargeLow, 10.0);
        QCOMPARE(*s.runtimeLow, 180);
        QCOMPARE(*s.wattsOut, 173.0);
        QCOMPARE(s.model, QStringLiteral("Back-UPS RS 1500G"));
        QCOMPARE(s.serial, QStringLiteral("3B0000X00000"));
    }

    void statusTokens_data()
    {
        QTest::addColumn<QString>("status");
        QTest::addColumn<QString>("charge");
        QTest::addColumn<QString>("tokens");
        QTest::addColumn<bool>("commLost");

        QTest::newRow("charging") << "ONLINE" << "80.0 Percent" << "OL CHRG" << false;
        QTest::newRow("on battery") << "ONBATT" << "80.0 Percent" << "OB DISCHRG" << false;
        QTest::newRow("low") << "ONBATT LOWBATT" << "8.0 Percent" << "OB LB DISCHRG" << false;
        QTest::newRow("replace") << "ONLINE REPLACEBATT" << "100 Percent" << "OL RB" << false;
        QTest::newRow("shutting down") << "SHUTTING DOWN" << "5 Percent" << "FSD" << false;
        QTest::newRow("commlost") << "COMMLOST" << "100 Percent" << "" << true;
    }

    void statusTokens()
    {
        QFETCH(QString, status);
        QFETCH(QString, charge);
        QFETCH(QString, tokens);
        QFETCH(bool, commLost);
        const auto result = statusFromValues({ { "STATUS", status }, { "BCHARGE", charge } });
        QCOMPARE(result.commLost, commLost);
        QCOMPARE(result.status.flags, parseStatusTokens(tokens));
    }

    void sourcePolls()
    {
        FakeNis server;
        ApcupsdSource source(apcConfig(server.serverPort()));
        QSignalSpy status(&source, &Source::statusUpdated);
        source.start();
        QVERIFY(status.wait(5000));
        QVERIFY(source.lastStatus().online());
        QCOMPARE(source.connectionState(), ConnectionState::Connected);
        QCOMPARE(source.describe(), QStringLiteral("127.0.0.1:%1").arg(server.serverPort()));

        server.report.replace(3, "STATUS   : ONBATT ");
        source.pollNow();
        QVERIFY(status.wait(5000));
        QVERIFY(source.lastStatus().onBattery());
        QCOMPARE(server.connections, 2);  // one connection per poll
    }

    void commLostIsStale()
    {
        FakeNis server;
        server.report.replace(3, "STATUS   : COMMLOST ");
        ApcupsdSource source(apcConfig(server.serverPort()));
        QSignalSpy errors(&source, &Source::errorOccurred);
        source.start();
        QVERIFY(errors.wait(5000));
        QCOMPARE(errors.first().at(0).value<SourceError>(), SourceError::DataStale);
    }

    void truncatedReplyIsNetworkError()
    {
        FakeNis server;
        server.truncate = true;
        ApcupsdSource source(apcConfig(server.serverPort()));
        QSignalSpy errors(&source, &Source::errorOccurred);
        source.start();
        QVERIFY(errors.wait(5000));
        QCOMPARE(errors.first().at(0).value<SourceError>(), SourceError::Network);
    }

    void refusedIsNetworkError()
    {
        quint16 port = 0;
        {
            FakeNis closed;
            port = closed.serverPort();
        }
        ApcupsdSource source(apcConfig(port));
        QSignalSpy errors(&source, &Source::errorOccurred);
        source.start();
        QVERIFY(errors.wait(5000));
        QCOMPARE(errors.first().at(0).value<SourceError>(), SourceError::Network);
        QCOMPARE(source.connectionState(), ConnectionState::Error);
    }
};

QTEST_GUILESS_MAIN(TestApcupsd)
#include "tst_apcupsd.moc"
