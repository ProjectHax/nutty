// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "common/FakeHttpServer.h"
#include "core/sources/http/HttpJsonSource.h"
#include "core/sources/http/JsonPath.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>

using namespace nutty;

namespace {

QJsonValue parse(const char* text)
{
    const QJsonDocument doc = QJsonDocument::fromJson(text);
    return doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
}

DeviceConfig httpConfig(const QString& url, QVariantMap mapping, QVariantMap secrets = {})
{
    DeviceConfig c;
    c.id = QUuid::createUuid();
    c.type = SourceType::HttpJson;
    c.pollIntervalSec = 60;
    c.settings = std::move(mapping);
    c.settings.insert(keys::HttpUrl, url);
    c.secrets = std::move(secrets);
    return c;
}

QByteArray haState(const QString& entity, const QString& state, const QString& unit = {})
{
    QJsonObject attributes;
    if (!unit.isEmpty())
        attributes.insert("unit_of_measurement", unit);
    return QJsonDocument(
        QJsonObject { { "entity_id", entity }, { "state", state }, { "attributes", attributes } })
        .toJson(QJsonDocument::Compact);
}

}  // namespace

class TestHttpJson : public QObject
{
    Q_OBJECT

private slots:
    void resolvePaths_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("expected");  // toText of the result; "<none>" when missing

        QTest::newRow("member") << "battery.soc" << "87";
        QTest::newRow("index") << "cells[1].v" << "3.31";
        QTest::newRow("quoted dotted name") << R"(["pd.soc"])" << "55";
        QTest::newRow("single quotes") << "['pd.soc']" << "55";
        QTest::newRow("nested after quoted") << R"(meta["a.b"].c)" << "deep";
        QTest::newRow("string") << "state" << "On Battery";
        QTest::newRow("bool") << "flags.charging" << "true";
        QTest::newRow("missing member") << "battery.nope" << "<none>";
        QTest::newRow("index out of range") << "cells[5].v" << "<none>";
        QTest::newRow("index on object") << "battery[0]" << "<none>";
        QTest::newRow("unterminated") << "cells[0" << "<none>";
    }

    void resolvePaths()
    {
        QFETCH(QString, path);
        QFETCH(QString, expected);
        const QJsonValue root = parse(R"({
            "battery": {"soc": 87},
            "cells": [{"v": 3.30}, {"v": 3.31}],
            "pd.soc": 55,
            "meta": {"a.b": {"c": "deep"}},
            "state": "On Battery",
            "flags": {"charging": true}
        })");
        const auto v = json::resolve(root, path);
        QCOMPARE(v ? json::toText(*v) : QStringLiteral("<none>"), expected);
    }

    void numbers()
    {
        QCOMPARE(json::toNumber(QJsonValue(12.5)), std::optional<double>(12.5));
        QCOMPARE(json::toNumber(QJsonValue("87 %")), std::optional<double>(87));
        QCOMPARE(json::toNumber(QJsonValue(" 230.1V")), std::optional<double>(230.1));
        QCOMPARE(json::toNumber(QJsonValue("-4")), std::optional<double>(-4));
        QCOMPARE(json::toNumber(QJsonValue(true)), std::optional<double>(1));
        QVERIFY(!json::toNumber(QJsonValue("unavailable")));
        QVERIFY(!json::toNumber(QJsonValue()));
        QCOMPARE(json::toText(QJsonValue(100.0)), QStringLiteral("100"));
        QCOMPARE(json::toText(QJsonValue(99.5)), QStringLiteral("99.5"));
    }

    void buildStatusUnitsAndFlags()
    {
        const QVariantMap settings {
            { keys::HttpRuntimeUnit, "min" },
            { keys::HttpOnBatteryValues, "discharging, On Battery" },
            { keys::HttpChargingValues, "charging" },
            { keys::HttpLowBatteryValues, "low" },
            { keys::HttpOnlineValues, "online,charging,full" },
        };
        const auto build = [&](const char* state) {
            return HttpJsonSource::buildStatus(
                { { keys::HttpCharge, QJsonValue("64") }, { keys::HttpRuntime, QJsonValue(12.5) },
                    { keys::HttpStatus, QJsonValue(state) } },
                settings);
        };
        DeviceStatus s = build("ON BATTERY");
        QVERIFY(s.onBattery());
        QVERIFY(s.has(StatusFlag::Discharging));
        QCOMPARE(*s.charge, 64.0);
        QCOMPARE(*s.runtime, 750);
        QCOMPARE(s.raw.value(keys::HttpStatus), QStringLiteral("ON BATTERY"));

        s = build("charging");
        QVERIFY(s.online());
        QVERIFY(s.has(StatusFlag::Charging));

        s = build("low");
        QVERIFY(s.has(StatusFlag::LowBattery));
        QVERIFY(!s.online());  // not in the online list

        s = build("full");
        QCOMPARE(s.flags, StatusFlags(StatusFlag::Online));
    }

    void buildStatusDefaults()
    {
        // No status mapped: assume mains.
        DeviceStatus s = HttpJsonSource::buildStatus({ { keys::HttpCharge, QJsonValue(50) } }, {});
        QCOMPARE(s.flags, StatusFlags(StatusFlag::Online));

        // A status but no value lists: NUT-style tokens are understood.
        s = HttpJsonSource::buildStatus({ { keys::HttpStatus, QJsonValue("ob dischrg") } }, {});
        QVERIFY(s.onBattery());
        s = HttpJsonSource::buildStatus({ { keys::HttpStatus, QJsonValue("whatever") } }, {});
        QVERIFY(s.online());

        // Runtime unit from the hint (Home Assistant) when not configured.
        s = HttpJsonSource::buildStatus({ { keys::HttpRuntime, QJsonValue("2") } }, {}, QStringLiteral("h"));
        QCOMPARE(*s.runtime, 7200);
        s = HttpJsonSource::buildStatus({ { keys::HttpRuntime, QJsonValue("90") } }, {});
        QCOMPARE(*s.runtime, 90);
    }

    void genericSource()
    {
        FakeHttpServer server;
        server.setHandler([](const FakeHttpServer::Request& r) -> FakeHttpServer::Response {
            if (r.path() != "/status")
                return { 404, "{}" };
            return { 200, R"({"data":{"battery":{"soc":"72 %","runtime":1500},"load":31,"state":"OB"}})" };
        });
        HttpJsonSource source(httpConfig(server.baseUrl() + "/status",
            { { keys::HttpCharge, "data.battery.soc" }, { keys::HttpRuntime, "data.battery.runtime" },
                { keys::HttpLoad, "data.load" }, { keys::HttpStatus, "data.state" },
                { keys::HttpOnBatteryValues, "ob" },
                { keys::HttpHeaders, "X-Api-Key: abc\nbad line\nX-Other:  two words " } },
            { { keys::HttpToken, "tok" } }));
        QSignalSpy status(&source, &Source::statusUpdated);
        source.start();
        QVERIFY(status.wait(5000));
        const DeviceStatus s = source.lastStatus();
        QVERIFY(s.onBattery());
        QCOMPARE(*s.charge, 72.0);
        QCOMPARE(*s.runtime, 1500);
        QCOMPARE(*s.load, 31.0);
        QVERIFY(!s.wattsOut);

        const auto& r = server.requests().first();
        QCOMPARE(r.headers.value("authorization"), QByteArray("Bearer tok"));
        QCOMPARE(r.headers.value("x-api-key"), QByteArray("abc"));
        QCOMPARE(r.headers.value("x-other"), QByteArray("two words"));
        QCOMPARE(source.describe(), QStringLiteral("127.0.0.1/status"));
    }

    void explicitAuthorizationHeaderWins()
    {
        FakeHttpServer server;
        server.setHandler(
            [](const FakeHttpServer::Request&) -> FakeHttpServer::Response { return { 200, "{}" }; });
        HttpJsonSource source(httpConfig(server.baseUrl(),
            { { keys::HttpHeaders, "Authorization: Basic eDp5" } }, { { keys::HttpToken, "tok" } }));
        QSignalSpy status(&source, &Source::statusUpdated);
        source.start();
        QVERIFY(status.wait(5000));
        QCOMPARE(server.requests().first().headers.value("authorization"), QByteArray("Basic eDp5"));
    }

    void genericErrors_data()
    {
        QTest::addColumn<int>("http");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<SourceError>("kind");
        QTest::newRow("401") << 401 << QByteArray("{}") << SourceError::Auth;
        QTest::newRow("403") << 403 << QByteArray("{}") << SourceError::Auth;
        QTest::newRow("404") << 404 << QByteArray("{}") << SourceError::NotFound;
        QTest::newRow("500") << 500 << QByteArray("{}") << SourceError::Network;
        QTest::newRow("bad json") << 200 << QByteArray("<html>") << SourceError::Protocol;
    }

    void genericErrors()
    {
        QFETCH(int, http);
        QFETCH(QByteArray, body);
        QFETCH(SourceError, kind);
        FakeHttpServer server;
        server.setHandler(
            [&](const FakeHttpServer::Request&) -> FakeHttpServer::Response { return { http, body }; });
        HttpJsonSource source(httpConfig(server.baseUrl(), { { keys::HttpCharge, "soc" } }));
        QSignalSpy errors(&source, &Source::errorOccurred);
        source.start();
        QVERIFY(errors.wait(5000));
        QCOMPARE(errors.first().at(0).value<SourceError>(), kind);
    }

    void invalidUrlIsConfigError()
    {
        HttpJsonSource source(httpConfig("not a url", {}));
        QSignalSpy errors(&source, &Source::errorOccurred);
        source.start();
        QCOMPARE(errors.size(), 1);
        QCOMPARE(errors.first().at(0).value<SourceError>(), SourceError::Config);
    }

    void homeAssistant()
    {
        FakeHttpServer server;
        server.setHandler([](const FakeHttpServer::Request& r) -> FakeHttpServer::Response {
            if (r.headers.value("authorization") != "Bearer long-lived")
                return { 401, R"({"message":"Unauthorized"})" };
            if (r.path() == "/api/states/sensor.ups_battery_charge")
                return { 200, haState("sensor.ups_battery_charge", "64", "%") };
            if (r.path() == "/api/states/sensor.ups_battery_runtime")
                return { 200, haState("sensor.ups_battery_runtime", "25", "min") };
            if (r.path() == "/api/states/sensor.ups_status")
                return { 200, haState("sensor.ups_status", "On Battery Battery Discharging") };
            if (r.path() == "/api/states/sensor.ups_load")
                return { 200, haState("sensor.ups_load", "unavailable", "%") };
            return { 404, R"({"message":"Entity not found."})" };
        });
        HttpJsonSource source(httpConfig(server.baseUrl() + "/",
            { { keys::HttpPreset, "homeassistant" }, { keys::HttpCharge, "sensor.ups_battery_charge" },
                { keys::HttpRuntime, "sensor.ups_battery_runtime" }, { keys::HttpLoad, "sensor.ups_load" },
                { keys::HttpStatus, "sensor.ups_status" },
                { keys::HttpOnBatteryValues, "On Battery Battery Discharging" } },
            { { keys::HttpToken, "long-lived" } }));
        QSignalSpy status(&source, &Source::statusUpdated);
        source.start();
        QVERIFY(status.wait(5000));
        const DeviceStatus s = source.lastStatus();
        QVERIFY(s.onBattery());
        QCOMPARE(*s.charge, 64.0);
        QCOMPARE(*s.runtime, 25 * 60);  // unit_of_measurement
        QVERIFY(!s.load);  // "unavailable"
        QCOMPARE(server.requests().size(), 4);
        for (const auto& r : server.requests())
            QVERIFY(r.path().startsWith("/api/states/sensor."));
    }

    void homeAssistantErrors()
    {
        FakeHttpServer server;
        server.setHandler([](const FakeHttpServer::Request& r) -> FakeHttpServer::Response {
            if (r.path() == "/api/states/sensor.good")
                return { 200, haState("sensor.good", "50") };
            return { 404, "{}" };
        });

        // One entity missing: the rest is still published.
        {
            HttpJsonSource source(httpConfig(server.baseUrl(),
                { { keys::HttpPreset, "homeassistant" }, { keys::HttpCharge, "sensor.good" },
                    { keys::HttpRuntime, "sensor.missing" } }));
            QSignalSpy status(&source, &Source::statusUpdated);
            source.start();
            QVERIFY(status.wait(5000));
            QCOMPARE(*source.lastStatus().charge, 50.0);
        }
        // Every entity missing: NotFound.
        {
            HttpJsonSource source(httpConfig(server.baseUrl(),
                { { keys::HttpPreset, "homeassistant" }, { keys::HttpCharge, "sensor.missing" } }));
            QSignalSpy errors(&source, &Source::errorOccurred);
            source.start();
            QVERIFY(errors.wait(5000));
            QCOMPARE(errors.first().at(0).value<SourceError>(), SourceError::NotFound);
        }
        // Nothing mapped: Config.
        {
            HttpJsonSource source(httpConfig(server.baseUrl(), { { keys::HttpPreset, "homeassistant" } }));
            QSignalSpy errors(&source, &Source::errorOccurred);
            source.start();
            QCOMPARE(errors.size(), 1);
            QCOMPARE(errors.first().at(0).value<SourceError>(), SourceError::Config);
        }
    }

    void homeAssistantBadTokenIsAuth()
    {
        FakeHttpServer server;
        server.setHandler([](const FakeHttpServer::Request& r) -> FakeHttpServer::Response {
            if (r.path() == "/api/states/sensor.a")
                return { 200, haState("sensor.a", "50") };
            return { 401, "{}" };
        });
        HttpJsonSource source(httpConfig(server.baseUrl(),
            { { keys::HttpPreset, "homeassistant" }, { keys::HttpCharge, "sensor.a" },
                { keys::HttpRuntime, "sensor.b" } }));
        QSignalSpy errors(&source, &Source::errorOccurred);
        QSignalSpy status(&source, &Source::statusUpdated);
        source.start();
        QVERIFY(errors.wait(5000));
        QCOMPARE(errors.first().at(0).value<SourceError>(), SourceError::Auth);
        QCOMPARE(status.size(), 0);  // auth problems are never papered over
    }
};

QTEST_GUILESS_MAIN(TestHttpJson)
#include "tst_httpjson.moc"
