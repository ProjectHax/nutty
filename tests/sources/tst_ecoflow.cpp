// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "common/FakeHttpServer.h"
#include "core/sources/ecoflow/EcoFlowClient.h"
#include "core/sources/ecoflow/EcoFlowMapping.h"
#include "core/sources/ecoflow/EcoFlowSigner.h"
#include "core/sources/ecoflow/EcoFlowSource.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <QUrlQuery>
#include <QUuid>

using namespace nutty;
using namespace nutty::ecoflow;

namespace {

// The worked example from EcoFlow's Open Platform documentation.
constexpr auto kDocAccessKey = "Fp4SvIprYSDPXtYJidEtUAd1o";
constexpr auto kDocSecretKey = "WIbFEKre0s6sLnh4ei7SPUeYnptHG6V";

constexpr auto kSerial = "TESTSERIAL0001";
constexpr auto kAccessKey = "test-access-key";
constexpr auto kSecretKey = "test-secret-key";

QJsonObject json(const char* text)
{
    return QJsonDocument::fromJson(text).object();
}

QByteArray envelope(
    const QJsonValue& data, const QString& code = QStringLiteral("0"), const QString& message = {})
{
    QJsonObject o { { "code", code },
        { "message", message.isEmpty() ? QStringLiteral("Success") : message } };
    if (!data.isUndefined())
        o.insert("data", data);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

DeviceConfig ecoConfig(const FakeHttpServer& server)
{
    DeviceConfig c;
    c.id = QUuid::createUuid();
    c.type = SourceType::EcoFlow;
    c.pollIntervalSec = 60;
    c.settings = { { keys::EcoRegion, "custom" }, { keys::EcoHost, server.baseUrl() },
        { keys::EcoSerial, kSerial } };
    c.secrets = { { keys::EcoAccessKey, kAccessKey }, { keys::EcoSecretKey, kSecretKey } };
    return c;
}

// Checks the four signing headers the way EcoFlow's server does.
bool signatureValid(const FakeHttpServer::Request& request)
{
    const QUrlQuery query(QString::fromUtf8(request.target.mid(request.target.indexOf('?') + 1)));
    QJsonObject params;
    if (request.target.contains('?')) {
        for (const auto& [k, v] : query.queryItems(QUrl::FullyDecoded))
            params.insert(k, v);
    }
    const QString nonce = QString::fromUtf8(request.headers.value("nonce"));
    const QString timestamp = QString::fromUtf8(request.headers.value("timestamp"));
    const Signature expected = signRequest(params, kAccessKey, kSecretKey, nonce, timestamp);
    return request.headers.value("accesskey") == kAccessKey && !nonce.isEmpty() && !timestamp.isEmpty()
        && QString::fromUtf8(request.headers.value("sign")) == expected.sign;
}

const auto kDelta2OnBattery = R"({
    "bms_emsStatus.lcdShowSoc": 45,
    "bms_emsStatus.dsgRemainTime": 95,
    "bms_emsStatus.chgRemainTime": 5999,
    "bms_emsStatus.minDsgSoc": 10,
    "bms_bmsStatus.temp": 27,
    "pd.wattsInSum": 0,
    "pd.wattsOutSum": 230,
    "inv.inputWatts": 0,
    "inv.acInVol": 0,
    "inv.invOutVol": 230120
})";

// A made-up DELTA Pro: AC on, X-Boost off, 12 V on, beeper on (EcoFlow
// reports that as 0), backup reserve off, one extra battery (not plugged in).
const auto kDeltaPro = R"({
    "ems.lcdShowSoc": 80,
    "ems.dsgRemainTime": 300,
    "ems.chgRemainTime": 45,
    "bmsMaster.soc": 80,
    "bmsMaster.soh": 97,
    "bmsMaster.cycles": 280,
    "bmsMaster.temp": 26,
    "bmsMaster.vol": 50000,
    "bmsMaster.amp": -2000,
    "bmsMaster.fullCap": 80000,
    "bmsMaster.designCap": 80000,
    "bmsMaster.remainCap": 64000,
    "bmsMaster.minCellVol": 3300,
    "bmsMaster.maxCellVol": 3310,
    "ems.bms1Online": 0,
    "bmsSlave1.soc": 50,
    "pd.wattsInSum": 620,
    "pd.wattsOutSum": 335,
    "inv.inputWatts": 500,
    "mppt.inWatts": 120,
    "inv.outputWatts": 300,
    "pd.usb1Watts": 5,
    "pd.usb2Watts": 0,
    "pd.typec1Watts": 30,
    "mppt.carOutWatts": 0,
    "pd.chgPowerAc": 1000,
    "pd.chgSunPower": 4294967295,
    "inv.outTemp": 31,
    "inv.acInVol": 230000,
    "inv.invOutVol": 230000,
    "inv.cfgAcEnabled": 1,
    "inv.cfgAcXboost": 0,
    "inv.cfgStandbyMin": 0,
    "inv.cfgSlowChgWatts": 400,
    "inv.cfgAcOutVoltage": 230000,
    "mppt.carState": 1,
    "mppt.cfgDcChgCurrent": 8000,
    "ems.maxChargeSoc": 100,
    "ems.minDsgSoc": 10,
    "ems.minOpenOilEbSoc": 5,
    "ems.maxCloseOilEbSoc": 100,
    "pd.watthisconfig": 0,
    "pd.bppowerSoc": 80,
    "pd.beepState": 0,
    "pd.lcdOffSec": 60,
    "pd.standByMode": 120
})";

// DELTA Pro serial numbers start with DCAB; the command table is gated on it.
constexpr auto kDeltaProSerial = "DCABTEST0000001";

std::optional<DeviceControl> findControl(const QList<DeviceControl>& controls, const QString& id)
{
    for (const DeviceControl& c : controls) {
        if (c.id == id)
            return c;
    }
    return std::nullopt;
}

double reading(const QList<DeviceDetails::Reading>& readings, const QString& id)
{
    for (const auto& r : readings) {
        if (r.id == id)
            return r.value;
    }
    return -1;
}

// An EcoFlow cloud for one DELTA Pro: quota/all reads `quota`, and a PUT to
// device/quota changes it the way the device would (unless `ignore` is set),
// or answers with `putCode`.
struct FakeDeltaPro {
    FakeHttpServer server;
    QJsonObject quota = json(kDeltaPro);
    QString putCode = QStringLiteral("0");
    bool ignore = false;

    FakeDeltaPro()
    {
        server.setHandler([this](const FakeHttpServer::Request& r) -> FakeHttpServer::Response {
            if (r.path() == "/iot-open/sign/device/list")
                return { 200,
                    envelope(QJsonArray { QJsonObject {
                        { "sn", kDeltaProSerial }, { "online", 1 }, { "productName", "DELTA Pro" } } }) };
            if (r.path() == "/iot-open/sign/device/quota/all")
                return { 200, envelope(quota) };
            if (r.method == "PUT" && r.path() == "/iot-open/sign/device/quota") {
                if (putCode != u"0")
                    return { 200, envelope(QJsonValue::Undefined, putCode, QStringLiteral("rejected")) };
                const QJsonObject params
                    = QJsonDocument::fromJson(r.body).object().value("params").toObject();
                if (!ignore && params.value("id").toInt() == 49)
                    quota.insert("ems.maxChargeSoc", params.value("maxChgSoc"));
                return { 200, envelope(QJsonValue::Undefined) };
            }
            return { 404, "{}" };
        });
    }

    DeviceConfig config() const
    {
        DeviceConfig c = ecoConfig(server);
        c.settings.insert(keys::EcoSerial, kDeltaProSerial);
        return c;
    }
    QList<FakeHttpServer::Request> puts() const
    {
        QList<FakeHttpServer::Request> out;
        for (const auto& r : server.requests()) {
            if (r.method == "PUT")
                out << r;
        }
        return out;
    }
};

}  // namespace

class TestEcoFlow : public QObject
{
    Q_OBJECT

private slots:
    void signatureMatchesDocumentation()
    {
        const QJsonObject params = json(R"({"sn":"123456789","params":{"cmdSet":11,"id":24,"eps":0}})");
        QCOMPARE(signaturePayload(flatten(params), kDocAccessKey, "345164", "1671171709428"),
            QByteArray("params.cmdSet=11&params.eps=0&params.id=24&sn=123456789"
                       "&accessKey=Fp4SvIprYSDPXtYJidEtUAd1o&nonce=345164&timestamp=1671171709428"));
        const Signature s = signRequest(params, kDocAccessKey, kDocSecretKey, "345164", "1671171709428");
        QCOMPARE(s.sign, QStringLiteral("07c13b65e037faf3b153d51613638fa80003c4c38d2407379a7f52851af1473e"));
    }

    void signatureWithoutParams()
    {
        QCOMPARE(signaturePayload({}, "ak", "1", "2"), QByteArray("accessKey=ak&nonce=1&timestamp=2"));
        const Signature random = signRequest({}, "ak", "sk");
        QCOMPARE(random.nonce.size(), 6);
        QVERIFY(random.timestamp.toLongLong() > 1'600'000'000'000LL);
        QCOMPARE(random.sign.size(), 64);
    }

    void flattening()
    {
        const auto params = flatten(json(R"({"z":true,"a":{"b":[1,{"c":"x"}],"d":1.5},"n":null})"));
        const QList<Param> expected { { "z", "true" }, { "a.b[0]", "1" }, { "a.b[1].c", "x" },
            { "a.d", "1.5" } };
        // Object member order is not guaranteed; compare as sets.
        QCOMPARE(params.size(), expected.size());
        for (const auto& p : expected)
            QVERIFY2(params.contains(p), qPrintable(p.first));
    }

    void baseUrls()
    {
        QCOMPARE(EcoFlowClient::baseUrl({}), QStringLiteral("https://api.ecoflow.com"));
        QCOMPARE(EcoFlowClient::baseUrl({ { keys::EcoRegion, "eu" } }),
            QStringLiteral("https://api-e.ecoflow.com"));
        QCOMPARE(EcoFlowClient::baseUrl(
                     { { keys::EcoRegion, "custom" }, { keys::EcoHost, "api-a.example.com/" } }),
            QStringLiteral("https://api-a.example.com"));
        QCOMPARE(EcoFlowClient::baseUrl({ { keys::EcoRegion, "custom" }, { keys::EcoHost, "" } }),
            QStringLiteral("https://api.ecoflow.com"));
    }

    void mappingResourceLoads()
    {
        QFile f(QString::fromLatin1(EcoFlowMapping::kResourcePath));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QJsonParseError error;
        QJsonDocument::fromJson(f.readAll(), &error);
        QCOMPARE(error.error, QJsonParseError::NoError);

        const auto& m = EcoFlowMapping::builtin();
        QVERIFY(m.isValid());
        for (const char* id : { "proto", "delta2", "deltapro", "powerstream", "smartplug", "generic" })
            QVERIFY2(m.familyIds().contains(QString::fromLatin1(id)), id);
        QCOMPARE(m.familyIds().last(), QStringLiteral("generic"));  // the catch-all goes last
    }

    void delta2OnBattery()
    {
        const auto r = EcoFlowMapping::builtin().map(json(kDelta2OnBattery));
        QCOMPARE(r.familyId, QStringLiteral("delta2"));
        const DeviceStatus& s = r.status;
        QVERIFY(s.onBattery());
        QVERIFY(s.has(StatusFlag::Discharging));
        QVERIFY(!s.has(StatusFlag::LowBattery));
        QCOMPARE(*s.charge, 45.0);
        QCOMPARE(*s.runtime, 95 * 60);
        QCOMPARE(*s.wattsOut, 230.0);
        QCOMPARE(*s.inputVoltage, 0.0);
        QCOMPARE(*s.outputVoltage, 230.12);  // mV -> V
        QCOMPARE(*s.temperature, 27.0);
        QCOMPARE(*s.chargeLow, 15.0);  // discharge limit + 5
        QCOMPARE(s.manufacturer, QStringLiteral("EcoFlow"));
        QCOMPARE(s.raw.value("pd.wattsOutSum"), QStringLiteral("230"));
    }

    void delta2LowBattery()
    {
        QJsonObject q = json(kDelta2OnBattery);
        q["bms_emsStatus.lcdShowSoc"] = 14;
        const auto s = EcoFlowMapping::builtin().map(q).status;
        QVERIFY(s.onBattery());
        QVERIFY(s.has(StatusFlag::LowBattery));
    }

    void delta2Charging()
    {
        QJsonObject q = json(kDelta2OnBattery);
        q["inv.acInVol"] = 121500;
        q["inv.inputWatts"] = 480;
        q["pd.wattsInSum"] = 480;
        q["pd.wattsOutSum"] = 60;
        const auto s = EcoFlowMapping::builtin().map(q).status;
        QVERIFY(s.online());
        QVERIFY(s.has(StatusFlag::Charging));
        QVERIFY(!s.onBattery());
        QCOMPARE(*s.inputVoltage, 121.5);
        QCOMPARE(*s.wattsIn, 480.0);
    }

    void protoFamily()
    {
        const auto r = EcoFlowMapping::builtin().map(json(R"({
            "cmsBattSoc": 80, "powInSumW": 0, "powOutSumW": 100, "powGetAcIn": 0,
            "plugInInfoAcInFeq": 0, "cmsDsgRemTime": 300, "cmsChgRemTime": 5999, "cmsMinDsgSoc": 0
        })"));
        QCOMPARE(r.familyId, QStringLiteral("proto"));
        QVERIFY(r.status.onBattery());
        QCOMPARE(*r.status.runtime, 300 * 60);
        QCOMPARE(*r.status.chargeLow, 10.0);  // no discharge limit set
    }

    void protoOnMainsWithoutVoltage()
    {
        // No voltage key: AC frequency alone says mains is present.
        const auto s = EcoFlowMapping::builtin()
                           .map(json(R"({"cmsBattSoc": 100, "powInSumW": 3, "powOutSumW": 40,
                                         "plugInInfoAcInFeq": 60})"))
                           .status;
        QVERIFY(s.online());
        QVERIFY(!s.has(StatusFlag::Charging));  // full
    }

    void invalidMinutes()
    {
        QVERIFY(!validMinutes(std::nullopt));
        QVERIFY(!validMinutes(-1));
        QVERIFY(!validMinutes(0));
        QVERIFY(!validMinutes(5999));
        QCOMPARE(validMinutes(59.6), std::optional<qint64>(60));

        QJsonObject q = json(kDelta2OnBattery);
        q["bms_emsStatus.dsgRemainTime"] = 5999;
        QVERIFY(!EcoFlowMapping::builtin().map(q).status.runtime);
    }

    void smartPlugHasNoBattery()
    {
        const auto r = EcoFlowMapping::builtin().map(json(R"({"2_1.watts": 1505, "2_1.volt": 120})"));
        QCOMPARE(r.familyId, QStringLiteral("smartplug"));
        QVERIFY(!r.hasBattery);
        QVERIFY(r.status.online());
        QCOMPARE(*r.status.wattsOut, 150.5);  // deciwatts
    }

    void genericFallback()
    {
        const auto r
            = EcoFlowMapping::builtin().map(json(R"({"soc": 60, "outputWatts": 90, "remainTime": -120})"));
        QCOMPARE(r.familyId, QStringLiteral("generic"));
        QVERIFY(r.status.onBattery());  // nothing in, power going out
        QCOMPARE(*r.status.charge, 60.0);
        QCOMPARE(*r.status.runtime, 120 * 60);  // |remainTime| while discharging
    }

    void customTable()
    {
        const auto m = EcoFlowMapping::fromJson(R"({"families":[
            {"id":"x","label":"X","match":["k"],"fields":{"charge":[{"key":"k","scale":0.5}]}}]})");
        QVERIFY(m.isValid());
        QCOMPARE(*m.map(json(R"({"k": 150})")).status.charge, 75.0);
        QVERIFY(!EcoFlowMapping::fromJson("not json").isValid());
    }

    void deltaProDetails()
    {
        const DeviceDetails d = EcoFlowMapping::builtin().details(json(kDeltaPro));
        QCOMPARE(reading(d.inputs, "ac"), 500.0);
        QCOMPARE(reading(d.inputs, "solar"), 120.0);
        QCOMPARE(reading(d.outputs, "ac"), 300.0);
        QCOMPARE(reading(d.outputs, "usb"), 5.0);  // the ports add up
        QCOMPARE(reading(d.outputs, "usbc"), 30.0);
        QCOMPARE(reading(d.outputs, "car"), 0.0);
        QCOMPARE(reading(d.outputs, "wireless"), -1.0);  // not reported

        // The extra battery reports a charge but isn't plugged in.
        QCOMPARE(d.packs.size(), 1);
        const DeviceDetails::Pack& main = d.packs.first();
        QCOMPARE(main.id, QStringLiteral("main"));
        QCOMPARE(*main.charge, 80.0);
        QCOMPARE(*main.health, 97.0);
        QCOMPARE(*main.cycles, 280);
        QCOMPARE(*main.voltage, 50.0);
        QCOMPARE(*main.current, -2.0);
        QCOMPARE(*main.fullCapacity, 80.0);
        QCOMPARE(*main.cellMin, 3.3);
        QCOMPARE(*main.cellMax, 3.31);

        // 2^32 - 1 is a counter the unit doesn't keep.
        QCOMPARE(reading(d.energy, "acIn"), 1000.0);
        QCOMPARE(reading(d.energy, "solarIn"), -1.0);
        QCOMPARE(reading(d.temperatures, "inverter"), 31.0);

        QVERIFY(EcoFlowMapping::builtin().details(json(kDelta2OnBattery)).packs.isEmpty());
    }

    void deltaProControls()
    {
        const auto& m = EcoFlowMapping::builtin();
        const QJsonObject quota = json(kDeltaPro);
        // Only for the serial numbers the commands were written for.
        QVERIFY(m.controls(quota, kSerial).isEmpty());
        QVERIFY(m.controls(json(kDelta2OnBattery), kDeltaProSerial).isEmpty());

        const QList<DeviceControl> controls = m.controls(quota, kDeltaProSerial);
        QCOMPARE(controls.size(), 15);
        QCOMPARE(*findControl(controls, "acOutput")->value, 1.0);
        QCOMPARE(*findControl(controls, "xBoost")->value, 0.0);
        QCOMPARE(*findControl(controls, "carOutput")->value, 1.0);
        QCOMPARE(*findControl(controls, "chargeLimit")->value, 100.0);
        QCOMPARE(*findControl(controls, "carInputCurrent")->value, 8000.0);
        QCOMPARE(*findControl(controls, "screenTimeout")->value, 60.0);
        // EcoFlow reports the beeper backwards: 0 is on.
        QCOMPARE(*findControl(controls, "beeper")->value, 1.0);

        const DeviceControl speed = *findControl(controls, "acChargeSpeed");
        QCOMPARE(speed.kind, ControlKind::Level);
        QCOMPARE(speed.maximum, 2900.0);
        QCOMPARE(speed.unit, QStringLiteral("W"));
        QVERIFY(speed.accepts(1500) && !speed.accepts(1550) && !speed.accepts(100));

        // A 120 V unit charges at 1800 W at most.
        QJsonObject us = quota;
        us.insert("inv.cfgAcOutVoltage", 120000);
        QCOMPARE(findControl(m.controls(us, kDeltaProSerial), "acChargeSpeed")->maximum, 1800.0);

        // A reading that isn't on or off leaves a toggle unknown.
        QJsonObject odd = quota;
        odd.insert("mppt.carState", 7);
        QVERIFY(!findControl(m.controls(odd, kDeltaProSerial), "carOutput")->value);
    }

    void deltaProAsks()
    {
        const QList<DeviceControl> controls
            = EcoFlowMapping::builtin().controls(json(kDeltaPro), kDeltaProSerial);
        const auto twice = [&](const char* id, double v) { return findControl(controls, id)->asksTwice(v); };
        QVERIFY(twice("acOutput", 0) && twice("acOutput", 1));
        QVERIFY(twice("xBoost", 1));
        QVERIFY(twice("acChargeSpeed", 1500));
        QVERIFY(twice("carOutput", 0) && !twice("carOutput", 1));
        QVERIFY(twice("acTimeout", 60) && !twice("acTimeout", 0));
        QVERIFY(twice("unitTimeout", 30) && !twice("unitTimeout", 0));
        QVERIFY(!twice("chargeLimit", 80));
        QVERIFY(!twice("beeper", 0));
    }

    void deltaProCommands_data()
    {
        QTest::addColumn<QString>("id");
        QTest::addColumn<double>("value");
        QTest::addColumn<QByteArray>("expected");

        // The AC command carries X-Boost as it is now, and the other way round.
        QTest::newRow("ac off") << "acOutput" << 0.0
                                << QByteArray(R"({"cmdSet":32,"enabled":0,"id":66,"xboost":0})");
        QTest::newRow("x-boost on") << "xBoost" << 1.0
                                    << QByteArray(R"({"cmdSet":32,"enabled":1,"id":66,"xboost":1})");
        QTest::newRow("12 V off") << "carOutput" << 0.0 << QByteArray(R"({"cmdSet":32,"enabled":0,"id":81})");
        QTest::newRow("beeper off") << "beeper" << 0.0 << QByteArray(R"({"cmdSet":32,"enabled":1,"id":38})");
        QTest::newRow("charge limit")
            << "chargeLimit" << 80.0 << QByteArray(R"({"cmdSet":32,"id":49,"maxChgSoc":80})");
        QTest::newRow("discharge limit")
            << "dischargeLimit" << 20.0 << QByteArray(R"({"cmdSet":32,"id":51,"minDsgSoc":20})");
        QTest::newRow("charge speed")
            << "acChargeSpeed" << 1500.0 << QByteArray(R"({"cmdSet":32,"id":69,"slowChgPower":1500})");
        QTest::newRow("car current") << "carInputCurrent" << 4000.0
                                     << QByteArray(R"({"cmdSet":32,"currMa":4000,"id":71})");
        QTest::newRow("reserve on")
            << "backupReserve" << 1.0
            << QByteArray(
                   R"({"bpPowerSoc":80,"cmdSet":32,"id":94,"isConfig":1,"maxChgSoc":0,"minDsgSoc":0})");
        QTest::newRow("screen") << "screenTimeout" << 300.0
                                << QByteArray(R"({"cmdSet":32,"id":39,"lcdTime":300})");
        QTest::newRow("unit") << "unitTimeout" << 0.0
                              << QByteArray(R"({"cmdSet":32,"id":33,"standByMode":0})");
        QTest::newRow("ac timeout") << "acTimeout" << 720.0
                                    << QByteArray(R"({"cmdSet":32,"id":153,"standByMins":720})");
        QTest::newRow("generator start")
            << "generatorStart" << 10.0 << QByteArray(R"({"cmdSet":32,"id":52,"openOilSoc":10})");
        QTest::newRow("generator stop")
            << "generatorStop" << 90.0 << QByteArray(R"({"closeOilSoc":90,"cmdSet":32,"id":53})");
    }

    void deltaProCommands()
    {
        QFETCH(QString, id);
        QFETCH(double, value);
        QFETCH(QByteArray, expected);
        const auto params = EcoFlowMapping::builtin().command(json(kDeltaPro), kDeltaProSerial, id, value);
        QVERIFY2(params.has_value(), params ? "" : qPrintable(params.error()));
        QCOMPARE(QJsonDocument(*params).toJson(QJsonDocument::Compact), expected);
    }

    void deltaProRefusals()
    {
        const auto& m = EcoFlowMapping::builtin();
        const QJsonObject quota = json(kDeltaPro);
        QVERIFY(!m.command(quota, kSerial, "chargeLimit", 80));  // not a DELTA Pro serial
        QVERIFY(!m.command(quota, kDeltaProSerial, "noSuchThing", 1));
        QVERIFY(!m.command(quota, kDeltaProSerial, "chargeLimit", 40));  // below 50
        QVERIFY(!m.command(quota, kDeltaProSerial, "chargeLimit", 80.5));
        QVERIFY(!m.command(quota, kDeltaProSerial, "acOutput", 2));
        QVERIFY(!m.command(quota, kDeltaProSerial, "carInputCurrent", 5000));
        QVERIFY(!m.command(quota, kDeltaProSerial, "chargeLimit", qQNaN()));

        QJsonObject us = quota;
        us.insert("inv.cfgAcOutVoltage", 120000);
        QVERIFY(!m.command(us, kDeltaProSerial, "acChargeSpeed", 2000));
        QVERIFY(m.command(us, kDeltaProSerial, "acChargeSpeed", 1800));

        // The reserve level only applies with the reserve on.
        QVERIFY(!m.command(quota, kDeltaProSerial, "backupReserveLevel", 50));
        QJsonObject reserve = quota;
        reserve.insert("pd.watthisconfig", 1);
        QVERIFY(m.command(reserve, kDeltaProSerial, "backupReserveLevel", 50));

        // AC output can't be sent without knowing X-Boost, or it'd change too.
        QJsonObject partial = quota;
        partial.remove("inv.cfgAcXboost");
        const auto missing = m.command(partial, kDeltaProSerial, "acOutput", 0);
        QVERIFY(!missing);
        QVERIFY(missing.error().contains("inv.cfgAcXboost"));
    }

    void putIsSignedOverTheBody()
    {
        FakeHttpServer server;
        server.setHandler([](const FakeHttpServer::Request&) -> FakeHttpServer::Response {
            return { 200, envelope(QJsonValue::Undefined) };
        });
        EcoFlowClient client(ecoConfig(server).settings, ecoConfig(server).secrets);
        const QJsonObject body { { "sn", kDeltaProSerial },
            { "params", QJsonObject { { "cmdSet", 32 }, { "id", 49 }, { "maxChgSoc", 80 } } } };
        bool done = false;
        client.put("/iot-open/sign/device/quota", body, [&](const EcoFlowClient::Result& r) {
            QVERIFY(r.has_value());
            done = true;
        });
        QTRY_VERIFY(done);
        const FakeHttpServer::Request& r = server.requests().first();
        QCOMPARE(r.method, QByteArray("PUT"));
        QCOMPARE(r.target, QByteArray("/iot-open/sign/device/quota"));
        QCOMPARE(r.headers.value("content-type"), QByteArray("application/json;charset=UTF-8"));
        QCOMPARE(QJsonDocument::fromJson(r.body).object(), body);
        // Signed the way a GET is, with the body's fields flattened in.
        const Signature expected = signRequest(body, kAccessKey, kSecretKey,
            QString::fromUtf8(r.headers.value("nonce")), QString::fromUtf8(r.headers.value("timestamp")));
        QCOMPARE(QString::fromUtf8(r.headers.value("sign")), expected.sign);
    }

    void sourceAppliesAndConfirms()
    {
        FakeDeltaPro cloud;
        EcoFlowSource source(cloud.config());
        source.setConfirmTiming(50, 2000);
        QSignalSpy status(&source, &Source::statusUpdated);
        QSignalSpy finished(&source, &Source::controlFinished);
        QString error;
        QVERIFY(!source.applyControl("chargeLimit", 80, &error));  // nothing read yet
        QVERIFY(!error.isEmpty());

        source.start();
        QVERIFY(status.wait(5000));
        QCOMPARE(*findControl(source.controls(), "chargeLimit")->value, 100.0);
        QCOMPARE(source.details().packs.size(), 1);

        QVERIFY2(source.applyControl("chargeLimit", 80, &error), qPrintable(error));
        QCOMPARE(*findControl(source.controls(), "chargeLimit")->pending, 80.0);
        // One change at a time.
        QVERIFY(!source.applyControl("dischargeLimit", 20, &error));

        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.first().at(0).toString(), QStringLiteral("chargeLimit"));
        QCOMPARE(finished.first().at(1).toDouble(), 80.0);
        QCOMPARE(finished.first().at(2).value<ControlResult>(), ControlResult::Applied);
        QCOMPARE(*findControl(source.controls(), "chargeLimit")->value, 80.0);
        QVERIFY(!findControl(source.controls(), "chargeLimit")->pending);

        const auto puts = cloud.puts();
        QCOMPARE(puts.size(), 1);
        QCOMPARE(QJsonDocument::fromJson(puts.first().body).toJson(QJsonDocument::Compact),
            QByteArray(R"({"params":{"cmdSet":32,"id":49,"maxChgSoc":80},"sn":"DCABTEST0000001"})"));
    }

    void sourceUnconfirmed()
    {
        FakeDeltaPro cloud;
        cloud.ignore = true;
        EcoFlowSource source(cloud.config());
        source.setConfirmTiming(50, 300);
        QSignalSpy status(&source, &Source::statusUpdated);
        QSignalSpy finished(&source, &Source::controlFinished);
        source.start();
        QVERIFY(status.wait(5000));
        QVERIFY(source.applyControl("chargeLimit", 80));
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.first().at(2).value<ControlResult>(), ControlResult::Unconfirmed);
        QCOMPARE(*findControl(source.controls(), "chargeLimit")->value, 100.0);
    }

    void sourceRejected()
    {
        FakeDeltaPro cloud;
        cloud.putCode = QStringLiteral("1006");
        EcoFlowSource source(cloud.config());
        QSignalSpy status(&source, &Source::statusUpdated);
        QSignalSpy finished(&source, &Source::controlFinished);
        source.start();
        QVERIFY(status.wait(5000));
        QVERIFY(source.applyControl("chargeLimit", 80));
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.first().at(2).value<ControlResult>(), ControlResult::Failed);
        QVERIFY(!finished.first().at(3).toString().isEmpty());
        // Free for the next change.
        QVERIFY(!findControl(source.controls(), "chargeLimit")->pending);
        cloud.putCode = QStringLiteral("0");
        QVERIFY(source.applyControl("chargeLimit", 90));
    }

    void sourceRefusesBadValues()
    {
        FakeDeltaPro cloud;
        EcoFlowSource source(cloud.config());
        QSignalSpy status(&source, &Source::statusUpdated);
        source.start();
        QVERIFY(status.wait(5000));
        QString error;
        QVERIFY(!source.applyControl("chargeLimit", 20, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(cloud.puts().isEmpty());
    }

    void sourcePollsSignedRequests()
    {
        FakeHttpServer server;
        server.setHandler([](const FakeHttpServer::Request& r) -> FakeHttpServer::Response {
            if (r.path() == "/iot-open/sign/device/list")
                return { 200,
                    envelope(QJsonArray {
                        QJsonObject { { "sn", kSerial }, { "online", 1 }, { "productName", "DELTA 2" } } }) };
            if (r.path() == "/iot-open/sign/device/quota/all")
                return { 200, envelope(json(kDelta2OnBattery)) };
            return { 404, "{}" };
        });

        EcoFlowSource source(ecoConfig(server));
        QSignalSpy status(&source, &Source::statusUpdated);
        source.start();
        QVERIFY(status.wait(5000));

        const DeviceStatus s = source.lastStatus();
        QVERIFY(s.onBattery());
        QCOMPARE(s.serial, QString::fromLatin1(kSerial));
        QVERIFY(!server.requests().isEmpty());
        bool sawQuota = false;
        for (const auto& r : server.requests()) {
            QVERIFY2(signatureValid(r), r.target.constData());
            if (r.path() == "/iot-open/sign/device/quota/all") {
                sawQuota = true;
                QVERIFY(r.target.contains(QByteArray("sn=") + kSerial));
            }
        }
        QVERIFY(sawQuota);
        QTRY_COMPARE(source.lastStatus().model, QStringLiteral("DELTA 2"));
    }

    void apiErrors_data()
    {
        QTest::addColumn<int>("http");
        QTest::addColumn<QString>("code");
        QTest::addColumn<QString>("message");
        QTest::addColumn<SourceError>("kind");

        QTest::newRow("http 401") << 401 << "0" << "" << SourceError::Auth;
        QTest::newRow("bad sign") << 200 << "8521" << "signature is wrong" << SourceError::Auth;
        QTest::newRow("not bound") << 200 << "6012" << "device not bound to user" << SourceError::NotFound;
        QTest::newRow("offline") << 200 << "6013" << "device is offline" << SourceError::Network;
        QTest::newRow("other") << 200 << "1006" << "rate limited" << SourceError::Protocol;
    }

    void apiErrors()
    {
        QFETCH(int, http);
        QFETCH(QString, code);
        QFETCH(QString, message);
        QFETCH(SourceError, kind);

        FakeHttpServer server;
        server.setHandler([&](const FakeHttpServer::Request& r) -> FakeHttpServer::Response {
            if (r.path() == "/iot-open/sign/device/list")
                return { 200, envelope(QJsonArray {}) };
            return { http, envelope(QJsonValue::Undefined, code, message) };
        });
        EcoFlowSource source(ecoConfig(server));
        QSignalSpy errors(&source, &Source::errorOccurred);
        source.start();
        QVERIFY(errors.wait(5000));
        QCOMPARE(errors.first().at(0).value<SourceError>(), kind);
    }

    void offlineDevice()
    {
        FakeHttpServer server;
        server.setHandler([](const FakeHttpServer::Request& r) -> FakeHttpServer::Response {
            if (r.path() == "/iot-open/sign/device/list")
                return { 200, envelope(QJsonArray { QJsonObject { { "sn", kSerial }, { "online", 0 } } }) };
            return { 200, envelope(json(kDelta2OnBattery)) };
        });
        EcoFlowSource source(ecoConfig(server));
        QSignalSpy errors(&source, &Source::errorOccurred);
        source.start();
        QVERIFY(errors.wait(5000));
        QCOMPARE(errors.first().at(0).value<SourceError>(), SourceError::Network);
    }

    void missingCredentials()
    {
        FakeHttpServer server;
        DeviceConfig c = ecoConfig(server);
        c.secrets.clear();
        EcoFlowSource source(c);
        QSignalSpy errors(&source, &Source::errorOccurred);
        source.start();
        QCOMPARE(errors.size(), 1);
        QCOMPARE(errors.first().at(0).value<SourceError>(), SourceError::Config);
        QVERIFY(server.requests().isEmpty());
    }

    void discovery()
    {
        FakeHttpServer server;
        server.setHandler([](const FakeHttpServer::Request&) -> FakeHttpServer::Response {
            return { 200,
                envelope(QJsonArray {
                    QJsonObject { { "sn", "A1" }, { "online", 1 }, { "productName", "RIVER 3" },
                        { "deviceName", "Desk" } },
                    QJsonObject { { "sn", "B2" }, { "online", 0 }, { "productName", "DELTA Pro" } },
                }) };
        });
        const DeviceConfig c = ecoConfig(server);
        QPointer<Discovery> d = new EcoFlowDiscovery(c.settings, c.secrets);
        QSignalSpy finished(d.data(), &Discovery::finished);
        QVERIFY(finished.wait(5000));
        const auto devices = finished.first().at(0).value<QList<DiscoveredDevice>>();
        QCOMPARE(devices.size(), 2);
        QCOMPARE(devices.at(0).name, QStringLiteral("Desk"));
        QCOMPARE(devices.at(0).description, QStringLiteral("RIVER 3"));
        QVERIFY(devices.at(0).online);
        QCOMPARE(devices.at(1).name, QStringLiteral("DELTA Pro"));
        QVERIFY(!devices.at(1).online);
        QTRY_VERIFY(d.isNull());
    }
};

QTEST_GUILESS_MAIN(TestEcoFlow)
#include "tst_ecoflow.moc"
