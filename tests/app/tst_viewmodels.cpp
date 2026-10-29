// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "app/viewmodels/AppContext.h"
#include "app/viewmodels/Catalog.h"
#include "app/viewmodels/DeviceControls.h"
#include "app/viewmodels/DeviceEditor.h"
#include "app/viewmodels/DeviceListModel.h"
#include "app/viewmodels/EventLogModel.h"
#include "app/viewmodels/RuleEditor.h"
#include "app/viewmodels/Summary.h"
#include "common/FakeSource.h"
#include "core/engine/Monitor.h"
#include "core/storage/HistoryStore.h"

#include <QDir>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace nutty;
using namespace nutty::test;

namespace {

int role(const QAbstractItemModel& model, const QByteArray& name)
{
    const auto names = model.roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it) {
        if (it.value() == name)
            return it.key();
    }
    return -1;
}

QVariant value(const QAbstractItemModel& model, int row, const QByteArray& name)
{
    return model.data(model.index(row, 0), role(model, name));
}

Event makeEvent(EventType type, const QUuid& device, const QString& title, const QDateTime& at)
{
    Event e;
    e.timestamp = at;
    e.type = type;
    e.severity = defaultSeverity(type);
    e.deviceId = device;
    e.deviceName = QStringLiteral("Rack");
    e.title = title;
    return e;
}

DeviceConfig ecoflow(const QString& name, const QString& accessKey, const QString& serial)
{
    DeviceConfig d = makeDevice(name, SourceType::EcoFlow, 30);
    d.settings.insert(QString::fromLatin1(keys::EcoSerial), serial);
    d.settings.insert(QString::fromLatin1(keys::EcoRegion), QStringLiteral("global"));
    d.secrets.insert(QString::fromLatin1(keys::EcoAccessKey), accessKey);
    d.secrets.insert(QString::fromLatin1(keys::EcoSecretKey), accessKey + QStringLiteral("-secret"));
    return d;
}

const QString kAccessKey = QString::fromLatin1(keys::EcoAccessKey);

}  // namespace

// The view-models on a real Monitor (with FakeSources) and an in-memory
// history, the way the app wires them.
class TestViewModels : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<HistoryStore> history;
    std::unique_ptr<Monitor> monitor;
    std::unique_ptr<AppContext> context;
    QUuid rack;

    FakeSource* fake() { return FakeSource::find(rack); }

private slots:
    void initTestCase()
    {
        FakeSource::registerFor(SourceType::Nut);
        FakeSource::registerFor(SourceType::EcoFlow);
        FakeDiscovery::registerFor(SourceType::EcoFlow,
            { { QStringLiteral("S1"), QStringLiteral("Shed station"), {}, true },
                { QStringLiteral("S2"), QStringLiteral("New station"), {}, true } });
    }

    void addDevices(const QList<DeviceConfig>& devices)
    {
        AppConfig config = context->config();
        config.devices << devices;
        context->setConfig(config);
    }

    void init()
    {
        history = std::make_unique<HistoryStore>();
        QVERIFY(history->open(QStringLiteral(":memory:")));
        monitor = std::make_unique<Monitor>(history.get());
        AppContext::Services services;
        services.monitor = monitor.get();
        services.history = history.get();
        context = std::make_unique<AppContext>(services);

        AppConfig config;
        DeviceConfig d = makeDevice(QStringLiteral("Rack"));
        rack = d.id;
        config.devices << d;
        context->setConfig(config);
        QVERIFY(fake());
    }

    void cleanup()
    {
        context.reset();
        monitor.reset();
        history.reset();
    }

    void deviceListFollowsTheSource()
    {
        DeviceListModel devices(context.get());
        QCOMPARE(devices.count(), 1);
        QCOMPARE(value(devices, 0, "name").toString(), QStringLiteral("Rack"));
        QCOMPARE(value(devices, 0, "deviceId").toString(), idString(rack));

        QSignalSpy changed(&devices, &QAbstractItemModel::dataChanged);
        fake()->push(onBattery(64, 900));
        QVERIFY(changed.count() > 0);
        QCOMPARE(value(devices, 0, "charge").toDouble(), 64.0);
        QCOMPARE(value(devices, 0, "status").toString(), QStringLiteral("onBattery"));
        QCOMPARE(devices.get(idString(rack)).value(QStringLiteral("runtime")).toLongLong(), 900);
    }

    void eventLogPagesFiltersAndInsertsLive()
    {
        const QDateTime start = QDateTime::currentDateTimeUtc().addSecs(-3600);
        for (int i = 0; i < 25; ++i) {
            const auto type = i % 2 == 0 ? EventType::OnBattery : EventType::CommLost;
            history->addEvent(
                makeEvent(type, rack, QStringLiteral("event %1").arg(i), start.addSecs(i * 10)));
        }

        EventLogModel log(context.get(), nullptr);
        log.setPageSize(10);
        log.reload();
        QTRY_COMPARE(log.count(), 10);
        QCOMPARE(log.total(), 25);
        QVERIFY(log.hasMore());
        // Newest first.
        QCOMPARE(value(log, 0, "title").toString(), QStringLiteral("event 24"));

        log.loadMore();
        QTRY_COMPARE(log.count(), 20);
        log.loadMore();
        QTRY_COMPARE(log.count(), 25);
        QVERIFY(!log.hasMore());

        log.setCategory(QStringLiteral("connection"));
        QTRY_COMPARE(log.total(), 12);
        QCOMPARE(value(log, 0, "type").toString(), QStringLiteral("CommLost"));

        log.setCategory(QStringLiteral("all"));
        log.setSearch(QStringLiteral("event 7"));
        QTRY_COMPARE(log.total(), 1);

        log.setSearch({});
        QTRY_COMPARE(log.total(), 25);
        const int before = log.count();
        history->addEvent(makeEvent(
            EventType::PowerRestored, rack, QStringLiteral("live"), QDateTime::currentDateTimeUtc()));
        QTRY_COMPARE(log.count(), before + 1);
        QCOMPARE(value(log, 0, "title").toString(), QStringLiteral("live"));
        QCOMPARE(value(log, 0, "severity").toString(), QStringLiteral("Info"));
    }

    void eventLogLimitTrimsLiveInserts()
    {
        EventLogModel log(context.get(), nullptr);
        log.setLimit(3);
        log.reload();
        for (int i = 0; i < 5; ++i)
            history->addEvent(
                makeEvent(EventType::OnBattery, rack, QString::number(i), QDateTime::currentDateTimeUtc()));
        QTRY_COMPARE(log.count(), 3);
        QCOMPARE(value(log, 0, "title").toString(), QStringLiteral("4"));
    }

    void summaryTracksTheWorstDevice()
    {
        Summary summary(context.get());
        fake()->push(online(100, 3600));
        summary.update();
        QCOMPARE(summary.deviceCount(), 1);
        QCOMPARE(summary.onlineCount(), 1);
        QCOMPARE(summary.level(), QStringLiteral("ok"));

        fake()->push(onBattery(40, 600));
        summary.update();
        QCOMPARE(summary.onBatteryCount(), 1);
        QVERIFY(summary.level() != QStringLiteral("ok"));
        QVERIFY(summary.title().contains(QStringLiteral("Rack"))
            || summary.subtitle().contains(QStringLiteral("Rack")));
    }

    void summaryWithoutDevicesOffersToAddOne()
    {
        context->setConfig(AppConfig {});
        Summary summary(context.get());
        summary.update();
        QCOMPARE(summary.deviceCount(), 0);
        QCOMPARE(summary.actionKey(), QStringLiteral("addDevice"));
    }

    void ruleEditorSavesThroughTheContext()
    {
        RuleEditor editor(context.get(), nullptr);
        editor.reset(idString(rack));
        QVERIFY(editor.isNew());
        QCOMPARE(editor.target(), idString(rack));
        editor.setCondition(QStringLiteral("ChargeBelow"));
        editor.setThreshold(25);
        editor.setAction(QStringLiteral("Shutdown"));
        editor.setCountdownSec(90);
        QCOMPARE(editor.unit(), QStringLiteral("%"));
        QVERIFY(editor.hasCountdown());
        QVERIFY(editor.save());
        QCOMPARE(context->config().rules.size(), 1);
        const Rule& r = context->config().rules.first();
        QCOMPARE(r.target, rack);
        QCOMPARE(r.condition, ConditionType::ChargeBelow);
        QCOMPARE(r.threshold, 25.0);
        QCOMPARE(r.countdownSec, 90);

        // Forced shutdown has no countdown.
        editor.setCondition(QStringLiteral("ForcedShutdown"));
        QVERIFY(!editor.hasCountdown());
    }

    void newDeviceReusesTheLastAccount()
    {
        // The last one has no keys (say the keychain lost them): it is skipped.
        DeviceConfig keyless = ecoflow(QStringLiteral("Keyless"), {}, QStringLiteral("S3"));
        keyless.secrets.clear();
        addDevices({ ecoflow(QStringLiteral("Garage"), QStringLiteral("k1"), QStringLiteral("S1")),
            ecoflow(QStringLiteral("Shed"), QStringLiteral("k2"), QStringLiteral("S2")), keyless });

        DeviceEditor editor(context.get(), nullptr);
        editor.reset(QStringLiteral("EcoFlow"));
        QCOMPARE(editor.reusedFrom(), QStringLiteral("Shed"));
        QCOMPARE(editor.value(kAccessKey).toString(), QStringLiteral("k2"));
        QCOMPARE(
            editor.value(QString::fromLatin1(keys::EcoSecretKey)).toString(), QStringLiteral("k2-secret"));
        QVERIFY(editor.value(QString::fromLatin1(keys::EcoSerial)).toString().isEmpty());
        QVERIFY(editor.accountNote().contains(QStringLiteral("Shed")));
        QVERIFY(editor.sharedWith().isEmpty());

        editor.startBlank();
        QVERIFY(editor.reusedFrom().isEmpty());
        QVERIFY(editor.accountNote().isEmpty());
        QVERIFY(editor.value(kAccessKey).toString().isEmpty());
        QCOMPARE(editor.value(QString::fromLatin1(keys::EcoRegion)).toString(), QStringLiteral("global"));

        // The NUT device from init() has no settings of its own: the new one
        // gets the defaults. Types without account details reuse nothing.
        editor.reset(QStringLiteral("Nut"));
        QCOMPARE(editor.reusedFrom(), QStringLiteral("Rack"));
        QCOMPARE(editor.value(QString::fromLatin1(keys::NutHost)).toString(), QStringLiteral("localhost"));
        editor.reset(QStringLiteral("System"));
        QVERIFY(editor.reusedFrom().isEmpty());
    }

    void discoveryMarksDevicesAlreadyAdded()
    {
        addDevices({ ecoflow(QStringLiteral("Shed"), QStringLiteral("k1"), QStringLiteral("S1")) });

        DeviceEditor editor(context.get(), nullptr);
        editor.reset(QStringLiteral("EcoFlow"));
        editor.discover();
        QTRY_COMPARE(editor.discovered().size(), 2);
        QVERIFY(editor.discovered().at(0).toMap().value(QStringLiteral("added")).toBool());
        QVERIFY(!editor.discovered().at(1).toMap().value(QStringLiteral("added")).toBool());

        // The same serial on another account is another device.
        editor.setValue(kAccessKey, QStringLiteral("other"));
        editor.discover();
        QTRY_COMPARE(editor.discovered().size(), 2);
        QVERIFY(!editor.discovered().at(0).toMap().value(QStringLiteral("added")).toBool());
    }

    void accountChangesCarryOverToSharedDevices()
    {
        const DeviceConfig garage
            = ecoflow(QStringLiteral("Garage"), QStringLiteral("k1"), QStringLiteral("S1"));
        const DeviceConfig shed = ecoflow(QStringLiteral("Shed"), QStringLiteral("k1"), QStringLiteral("S2"));
        const DeviceConfig cabin
            = ecoflow(QStringLiteral("Cabin"), QStringLiteral("k2"), QStringLiteral("S3"));
        addDevices({ garage, shed, cabin });
        const auto keyOf = [&](const QUuid& id) {
            return context->config().device(id)->secrets.value(kAccessKey).toString();
        };

        DeviceEditor editor(context.get(), nullptr);
        QVERIFY(editor.load(idString(garage.id)));
        QCOMPARE(editor.sharedWith(), QStringList { QStringLiteral("Shed") });
        QVERIFY(editor.accountNote().contains(QStringLiteral("Shed")));
        QVERIFY(!editor.accountEdited());

        editor.setValue(kAccessKey, QStringLiteral("k3"));
        QVERIFY(editor.accountEdited());
        QVERIFY(editor.save());
        QCOMPARE(keyOf(shed.id), QStringLiteral("k3"));
        QCOMPARE(keyOf(cabin.id), QStringLiteral("k2"));
        QCOMPARE(editor.sharedWith(), QStringList { QStringLiteral("Shed") });

        // Unless told not to: then the two go their own ways.
        editor.setValue(kAccessKey, QStringLiteral("k4"));
        editor.setApplyToShared(false);
        QVERIFY(editor.save());
        QCOMPARE(keyOf(garage.id), QStringLiteral("k4"));
        QCOMPARE(keyOf(shed.id), QStringLiteral("k3"));
        QVERIFY(editor.sharedWith().isEmpty());
    }

    void catalogGroupsAndRemoval()
    {
        Catalog catalog(context.get());
        QCOMPARE(catalog.devices().size(), 1);
        const QString group = catalog.saveGroup({}, QStringLiteral("Office"), { idString(rack) });
        QVERIFY(!group.isEmpty());
        QCOMPARE(catalog.groups().size(), 1);
        QCOMPARE(catalog.groups().first().toMap().value(QStringLiteral("memberNames")).toString(),
            QStringLiteral("Rack"));

        // Removing the device drops it from the group too.
        QVERIFY(catalog.removeDevice(idString(rack)));
        QCOMPARE(catalog.devices().size(), 0);
        QCOMPARE(catalog.groups().first().toMap().value(QStringLiteral("members")).toStringList().size(), 0);
    }

    void catalogChannelsCountChosenEvents()
    {
        AppConfig config = context->config();
        ChannelConfig desktop;
        desktop.id = QUuid::createUuid();
        desktop.type = ChannelType::Desktop;
        desktop.events = { EventType::OnBattery, EventType::PowerRestored, EventType::Test };
        ChannelConfig handWritten = desktop;
        handWritten.id = QUuid::createUuid();
        handWritten.events.remove(EventType::Test);
        config.channels << desktop << handWritten;
        context->setConfig(config);

        Catalog catalog(context.get());
        for (const QVariant& channel : catalog.channels()) {
            const QVariantMap map = channel.toMap();
            // The always-on test event isn't counted, whether or not it was saved.
            QCOMPARE(map.value(QStringLiteral("eventCount")).toInt(), 2);
            // A desktop channel has nothing worth repeating next to its type.
            QVERIFY(map.value(QStringLiteral("description")).toString().isEmpty());
        }
    }

    void deviceControlsAskAndApply()
    {
        DeviceControl ac;
        ac.id = QStringLiteral("acOutput");
        ac.value = 1;
        ac.ask = ControlAsk::Always;
        DeviceControl limit;
        limit.id = QStringLiteral("chargeLimit");
        limit.kind = ControlKind::Level;
        limit.minimum = 50;
        limit.unit = QStringLiteral("%");
        limit.value = 100;
        DeviceControl reserve;
        reserve.id = QStringLiteral("backupReserve");
        reserve.value = 0;
        DeviceControl level = limit;
        level.id = QStringLiteral("backupReserveLevel");
        level.dependsOn = reserve.id;
        DeviceControl screen;
        screen.id = QStringLiteral("screenTimeout");
        screen.kind = ControlKind::Choice;
        screen.choices = { 0, 10, 30 };
        screen.unit = QStringLiteral("s");
        screen.value = 60;  // set on the device itself
        fake()->settings = { ac, limit, reserve, level, screen };
        fake()->push(online());

        DeviceControls vm(context.get(), nullptr);
        vm.setDeviceId(idString(rack));
        QVERIFY(vm.available());
        QVERIFY(vm.blockedReason().isEmpty());
        const auto groups = vm.groups();
        QCOMPARE(groups.size(), 3);
        QCOMPARE(groups.at(0).toMap().value("key").toString(), QStringLiteral("outputs"));
        QCOMPARE(groups.at(1).toMap().value("key").toString(), QStringLiteral("battery"));
        QCOMPARE(groups.at(1).toMap().value("controls").toList().size(), 3);
        QCOMPARE(groups.at(2).toMap().value("key").toString(), QStringLiteral("unit"));

        const auto row = [&](int i) { return vm.controls().at(i).toMap(); };
        QVERIFY(row(0).value("enabled").toBool());
        QVERIFY(!row(3).value("enabled").toBool());  // the reserve is off
        QCOMPARE(row(4).value("options").toList().size(), 4);  // with the device's 60 s
        QVERIFY(!row(0).value("icon").toString().isEmpty());
        QVERIFY(!row(0).value("description").toString().isEmpty());

        // Asking says what changes; risky ones get a second question.
        const QVariantMap once = vm.confirmation(limit.id, 80);
        QVERIFY(!once.value("twice").toBool());
        QVERIFY(once.value("title").toString().contains(QStringLiteral("Rack")));
        QVERIFY(once.value("message").toString().contains(QStringLiteral("80")));
        const QVariantMap twice = vm.confirmation(ac.id, 0);
        QVERIFY(twice.value("twice").toBool());
        QCOMPARE(twice.value("confirmText").toString(), QStringLiteral("Continue"));
        QVERIFY(twice.value("warningMessage").toString().contains(QStringLiteral("Rack")));
        QVERIFY(fake()->applied.isEmpty());  // asking changes nothing

        QSignalSpy finished(&vm, &DeviceControls::finished);
        QVERIFY(vm.apply(limit.id, 80));
        QCOMPARE(fake()->applied.size(), 1);
        QVERIFY(row(1).value("pending").toBool());
        QVERIFY(!row(1).value("pendingText").toString().isEmpty());
        QVERIFY(!row(0).value("enabled").toBool());  // one change at a time

        fake()->finishControl(limit.id, 80, ControlResult::Applied);
        QCOMPARE(finished.size(), 1);
        QVERIFY(finished.first().at(1).toBool());
        QCOMPARE(row(1).value("result").toString(), QStringLiteral("Applied"));
        QCOMPARE(row(1).value("value").toDouble(), 80.0);
        QVERIFY(row(0).value("enabled").toBool());

        fake()->finishControl(ac.id, 0, ControlResult::Unconfirmed);
        QCOMPARE(row(0).value("result").toString(), QStringLiteral("Unconfirmed"));
        QVERIFY(!row(0).value("message").toString().isEmpty());

        // A refusal shows on the row.
        fake()->refusal = QStringLiteral("busy");
        QVERIFY(!vm.apply(limit.id, 90));
        QCOMPARE(row(1).value("result").toString(), QStringLiteral("Failed"));
        QCOMPARE(row(1).value("message").toString(), QStringLiteral("busy"));
        fake()->refusal.clear();

        // Nothing goes out while the device can't be reached or is paused.
        fake()->fail(SourceError::Network, QStringLiteral("gone"));
        QVERIFY(!vm.blockedReason().isEmpty());
        QVERIFY(!row(1).value("enabled").toBool());
        QVERIFY(!vm.apply(limit.id, 90));
        AppConfig config = context->config();
        config.devices.first().enabled = false;
        context->setConfig(config);
        QVERIFY(!vm.blockedReason().isEmpty());
        QVERIFY(!vm.apply(limit.id, 90));
        QCOMPARE(fake() ? fake()->applied.size() : 1, 1);
    }

    void idsRoundTrip()
    {
        const QUuid id = QUuid::createUuid();
        QCOMPARE(parseId(idString(id)), id);
        QVERIFY(!idString(id).startsWith(u'{'));
        QCOMPARE(expandPath(displayPath(QDir::homePath() + QStringLiteral("/x"))),
            QDir::homePath() + QStringLiteral("/x"));
    }
};

QTEST_MAIN(TestViewModels)
#include "tst_viewmodels.moc"
