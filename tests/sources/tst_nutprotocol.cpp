// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/sources/nut/NutProtocol.h"

#include <QTest>

using namespace nutty;
using namespace nutty::nut;

class TestNutProtocol : public QObject
{
    Q_OBJECT

private slots:
    void tokenize_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<QStringList>("words");

        QTest::newRow("plain") << "LIST VAR ups" << QStringList { "LIST", "VAR", "ups" };
        QTest::newRow("quoted") << "VAR ups device.model \"Back-UPS RS 1500\""
                                << QStringList { "VAR", "ups", "device.model", "Back-UPS RS 1500" };
        QTest::newRow("escapes") << "VAR ups ups.alarm \"say \\\"hi\\\" \\\\ ok\""
                                 << QStringList { "VAR", "ups", "ups.alarm", "say \"hi\" \\ ok" };
        QTest::newRow("empty value") << "VAR ups ups.alarm \"\""
                                     << QStringList { "VAR", "ups", "ups.alarm", "" };
        QTest::newRow("extra spaces") << "  OK   Goodbye  " << QStringList { "OK", "Goodbye" };
        QTest::newRow("empty") << "" << QStringList {};
    }

    void tokenize()
    {
        QFETCH(QString, line);
        QFETCH(QStringList, words);
        QCOMPARE(nut::tokenize(line), words);
    }

    void quoteAndCommand()
    {
        QCOMPARE(quote(u"ups"), QStringLiteral("ups"));
        QCOMPARE(quote(u""), QStringLiteral("\"\""));
        QCOMPARE(quote(u"two words"), QStringLiteral("\"two words\""));
        QCOMPARE(quote(u"pa\"ss\\"), QStringLiteral("\"pa\\\"ss\\\\\""));
        QCOMPARE(command({ "LIST", "VAR", "ups" }), QByteArray("LIST VAR ups\n"));
        QCOMPARE(command({ "PASSWORD", "p w" }), QByteArray("PASSWORD \"p w\"\n"));

        // Round trip: what we quote, tokenize() reads back.
        const QString nasty = QStringLiteral("a \"b\" \\c d");
        QCOMPARE(nut::tokenize(QString::fromUtf8(command({ "PASSWORD", nasty })).trimmed()),
            QStringList({ "PASSWORD", nasty }));
    }

    void parseLineKinds()
    {
        QCOMPARE(parseLine(u"OK").kind, LineKind::Ok);
        QCOMPARE(parseLine(u"OK Goodbye").kind, LineKind::Ok);
        const Line err = parseLine(u"ERR ACCESS-DENIED");
        QCOMPARE(err.kind, LineKind::Err);
        QCOMPARE(err.errorCode, QStringLiteral("ACCESS-DENIED"));
        QCOMPARE(parseLine(u"BEGIN LIST VAR ups").kind, LineKind::Begin);
        QCOMPARE(parseLine(u"END LIST VAR ups").kind, LineKind::End);
        QCOMPARE(parseLine(u"VAR ups battery.charge \"100\"").kind, LineKind::Var);
        QCOMPARE(parseLine(u"VAR ups battery.charge").kind, LineKind::Other);
        QCOMPARE(parseLine(u"UPS ups \"Living room\"").kind, LineKind::Ups);
        QCOMPARE(parseLine(u"Network UPS Tools upsd 2.8.1").kind, LineKind::Other);
    }

    void errorKinds()
    {
        QCOMPARE(errorKind(u"ACCESS-DENIED"), SourceError::Auth);
        QCOMPARE(errorKind(u"INVALID-PASSWORD"), SourceError::Auth);
        QCOMPARE(errorKind(u"ALREADY-SET-USERNAME"), SourceError::Auth);
        QCOMPARE(errorKind(u"UNKNOWN-UPS"), SourceError::NotFound);
        QCOMPARE(errorKind(u"DATA-STALE"), SourceError::DataStale);
        QCOMPARE(errorKind(u"DRIVER-NOT-CONNECTED"), SourceError::DataStale);
        QCOMPARE(errorKind(u"FEATURE-NOT-CONFIGURED"), SourceError::Protocol);
        QCOMPARE(errorKind(u"FEATURE-NOT-CONFIGURED", true), SourceError::Tls);
        QCOMPARE(errorKind(u"FEATURE-NOT-SUPPORTED", true), SourceError::Tls);
        QCOMPARE(errorKind(u"UNKNOWN-COMMAND"), SourceError::Protocol);
        QVERIFY(!errorDescription(u"DATA-STALE").isEmpty());
        QVERIFY(errorDescription(u"SOMETHING-NEW").contains(u"SOMETHING-NEW"));
    }

    void statusFromVariablesFull()
    {
        const QMap<QString, QString> vars {
            { "ups.status", "OB DISCHRG LB" },
            { "battery.charge", "18" },
            { "battery.charge.low", "20" },
            { "battery.runtime", "245.6" },
            { "battery.runtime.low", "120" },
            { "battery.voltage", "23.4" },
            { "ups.load", "40" },
            { "ups.realpower.nominal", "900" },
            { "input.voltage", "0.0" },
            { "output.voltage", "119.0" },
            { "battery.temperature", "31.5" },
            { "device.mfr", "Example" },
            { "ups.mfr", "Ignored" },
            { "device.model", "Model 1500" },
            { "ups.serial", "ABC123" },
            { "ups.alarm", "Replace battery!" },
        };
        const DeviceStatus s = statusFromVariables(vars);
        QVERIFY(s.onBattery());
        QVERIFY(s.has(StatusFlag::LowBattery));
        QVERIFY(s.has(StatusFlag::Discharging));
        QVERIFY(!s.online());
        QCOMPARE(*s.charge, 18.0);
        QCOMPARE(*s.chargeLow, 20.0);
        QCOMPARE(*s.runtime, 246);
        QCOMPARE(*s.runtimeLow, 120);
        QCOMPARE(*s.batteryVoltage, 23.4);
        QCOMPARE(*s.load, 40.0);
        QCOMPARE(*s.wattsOut, 360.0);  // nominal x load
        QCOMPARE(*s.inputVoltage, 0.0);
        QCOMPARE(*s.outputVoltage, 119.0);
        QCOMPARE(*s.temperature, 31.5);
        QCOMPARE(s.manufacturer, QStringLiteral("Example"));
        QCOMPARE(s.model, QStringLiteral("Model 1500"));
        QCOMPARE(s.serial, QStringLiteral("ABC123"));
        QCOMPARE(s.alarmText, QStringLiteral("Replace battery!"));
        QCOMPARE(s.raw.size(), vars.size());
    }

    void statusFromVariablesSparse()
    {
        const DeviceStatus s = statusFromVariables(
            { { "ups.status", "OL" }, { "ups.realpower", "123" }, { "ups.load", "x" } });
        QVERIFY(s.online());
        QVERIFY(!s.charge);
        QVERIFY(!s.runtime);
        QVERIFY(!s.load);
        QCOMPARE(*s.wattsOut, 123.0);  // realpower beats nominal x load
        QVERIFY(s.manufacturer.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestNutProtocol)
#include "tst_nutprotocol.moc"
