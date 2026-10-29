// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/system/SystemPowerSource.h"
#include "platform/system/SystemSources.h"

#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>

#include <memory>

using namespace nutty;

namespace {

class FakeBackend : public SystemPowerBackend
{
public:
    using SystemPowerBackend::SystemPowerBackend;

    bool available = true;
    bool readyFlag = true;
    QList<SystemPowerInfo> list;
    int refreshes = 0;

    bool isAvailable() const override { return available; }
    bool isReady() const override { return readyFlag; }
    QList<SystemPowerInfo> sources() const override { return list; }
    QString backendName() const override { return QStringLiteral("FakePower"); }
    void refresh() override
    {
        ++refreshes;
        if (readyFlag)
            QTimer::singleShot(0, this, &SystemPowerBackend::changed);
    }
    void becomeReady()
    {
        readyFlag = true;
        emit ready();
    }
};

SystemPowerInfo battery()
{
    SystemPowerInfo b;
    b.id = QStringLiteral("battery_BAT0");
    b.name = QStringLiteral("Battery BAT0");
    b.kind = SystemPowerInfo::Kind::Battery;
    b.state = SystemPowerInfo::State::Discharging;
    b.acOnline = false;
    b.charge = 76;
    b.timeToEmpty = 9000;
    b.watts = 8.5;
    b.voltage = 11.9;
    b.vendor = QStringLiteral("Example");
    b.model = QStringLiteral("5B10W");
    return b;
}

SystemPowerInfo ups()
{
    SystemPowerInfo u;
    u.id = QStringLiteral("ups_hiddev0");
    u.name = QStringLiteral("Example UPS 1500");
    u.kind = SystemPowerInfo::Kind::Ups;
    u.state = SystemPowerInfo::State::Full;
    u.acOnline = true;
    u.charge = 100;
    u.timeToEmpty = 2400;
    return u;
}

DeviceConfig systemConfig(const QString& sourceId = {})
{
    DeviceConfig c;
    c.id = QUuid::createUuid();
    c.type = SourceType::System;
    c.pollIntervalSec = 60;
    if (!sourceId.isEmpty())
        c.settings.insert(keys::SystemSourceId, sourceId);
    return c;
}

}  // namespace

class TestSystemPower : public QObject
{
    Q_OBJECT

private slots:
    void selection()
    {
        const QList<SystemPowerInfo> both { battery(), ups() };
        QCOMPARE(SystemPowerSource::select(both, {})->id, ups().id);  // a UPS first
        QCOMPARE(SystemPowerSource::select({ battery() }, {})->id, battery().id);
        QCOMPARE(SystemPowerSource::select(both, battery().id)->id, battery().id);
        QVERIFY(!SystemPowerSource::select(both, QStringLiteral("nope")));
        QVERIFY(!SystemPowerSource::select({}, {}));
    }

    void batteryDischarging()
    {
        const DeviceStatus s = SystemPowerSource::toStatus(battery());
        QVERIFY(s.onBattery());
        QVERIFY(s.has(StatusFlag::Discharging));
        QVERIFY(!s.online());
        QCOMPARE(*s.charge, 76.0);
        QCOMPARE(*s.runtime, 9000);
        QCOMPARE(*s.wattsOut, 8.5);
        QVERIFY(!s.wattsIn);
        QCOMPARE(*s.batteryVoltage, 11.9);
        QCOMPARE(s.manufacturer, QStringLiteral("Example"));
        QCOMPARE(s.model, QStringLiteral("5B10W"));
        QCOMPARE(s.raw.value("system.state"), QStringLiteral("discharging"));
    }

    void batteryStates()
    {
        SystemPowerInfo b = battery();

        // Plugged in but "discharging" (charge thresholds): still on mains.
        b.acOnline = true;
        DeviceStatus s = SystemPowerSource::toStatus(b);
        QVERIFY(s.online());
        QVERIFY(!s.runtime);

        b.state = SystemPowerInfo::State::Charging;
        s = SystemPowerSource::toStatus(b);
        QVERIFY(s.online());
        QVERIFY(s.has(StatusFlag::Charging));
        QCOMPARE(*s.wattsIn, 8.5);

        b.state = SystemPowerInfo::State::Unknown;
        b.acOnline = false;
        QVERIFY(SystemPowerSource::toStatus(b).onBattery());
        b.acOnline.reset();
        QVERIFY(SystemPowerSource::toStatus(b).online());

        b = battery();
        b.low = true;
        QVERIFY(SystemPowerSource::toStatus(b).has(StatusFlag::LowBattery));
        b.low = false;
        b.critical = true;
        QVERIFY(SystemPowerSource::toStatus(b).has(StatusFlag::LowBattery));
    }

    void upsUsesItsOwnState()
    {
        SystemPowerInfo u = ups();
        DeviceStatus s = SystemPowerSource::toStatus(u);
        QVERIFY(s.online());
        QVERIFY(!s.runtime);  // only while on battery
        QCOMPARE(s.model, u.name);

        // The computer's AC is the UPS output, so it says nothing here.
        u.state = SystemPowerInfo::State::Discharging;
        s = SystemPowerSource::toStatus(u);
        QVERIFY(s.onBattery());
        QCOMPARE(*s.runtime, 2400);
    }

    void sourcePublishes()
    {
        auto* backend = new FakeBackend;
        backend->list = { battery(), ups() };
        SystemPowerSource source(systemConfig(), backend);
        QSignalSpy status(&source, &Source::statusUpdated);
        source.start();
        QVERIFY(status.wait(2000));
        QCOMPARE(source.lastStatus().model, ups().name);
        QCOMPARE(source.describe(), ups().name);
        QCOMPARE(source.connectionState(), ConnectionState::Connected);

        // A change pushed by the OS is published without waiting for a poll.
        backend->list[1].state = SystemPowerInfo::State::Discharging;
        emit backend->changed();
        QCOMPARE(status.size(), 2);
        QVERIFY(source.lastStatus().onBattery());

        source.stop();
        emit backend->changed();
        QCOMPARE(status.size(), 2);  // stopped sources stay quiet
    }

    void sourceWaitsForReady()
    {
        auto* backend = new FakeBackend;
        backend->readyFlag = false;
        backend->list = { battery() };
        SystemPowerSource source(systemConfig(), backend);
        QSignalSpy status(&source, &Source::statusUpdated);
        source.start();
        QCOMPARE(backend->refreshes, 1);
        QTest::qWait(50);
        QCOMPARE(status.size(), 0);
        backend->becomeReady();
        QCOMPARE(status.size(), 1);
    }

    void sourceErrors()
    {
        {
            auto* backend = new FakeBackend;
            backend->available = false;
            SystemPowerSource source(systemConfig(), backend);
            QSignalSpy errors(&source, &Source::errorOccurred);
            source.start();
            QVERIFY(errors.wait(2000));
            QCOMPARE(errors.first().at(0).value<SourceError>(), SourceError::NotFound);
            QVERIFY(errors.first().at(1).toString().contains(u"FakePower"));
        }
        {
            auto* backend = new FakeBackend;
            backend->list = { battery() };
            SystemPowerSource source(systemConfig(QStringLiteral("ups_gone")), backend);
            QSignalSpy errors(&source, &Source::errorOccurred);
            source.start();
            QVERIFY(errors.wait(2000));
            QCOMPARE(errors.first().at(0).value<SourceError>(), SourceError::NotFound);
        }
        {
            auto* backend = new FakeBackend;
            SystemPowerInfo b = battery();
            b.present = false;
            backend->list = { b };
            SystemPowerSource source(systemConfig(), backend);
            QSignalSpy errors(&source, &Source::errorOccurred);
            source.start();
            QVERIFY(errors.wait(2000));
            QCOMPARE(source.describe(), b.name);
        }
    }

    void discovery()
    {
        auto* backend = new FakeBackend;
        backend->list = { battery(), ups() };
        QPointer<Discovery> d = new SystemDiscovery(backend);
        QSignalSpy finished(d.data(), &Discovery::finished);
        QVERIFY(finished.wait(2000));
        const auto devices = finished.first().at(0).value<QList<DiscoveredDevice>>();
        QCOMPARE(devices.size(), 2);
        QCOMPARE(devices.at(0).id, battery().id);
        QVERIFY(devices.at(0).description.contains(u"5B10W"));
        QCOMPARE(devices.at(1).name, ups().name);
        QTRY_VERIFY(d.isNull());
    }

    void discoveryUnavailable()
    {
        auto* backend = new FakeBackend;
        backend->available = false;
        QPointer<Discovery> d = new SystemDiscovery(backend);
        QSignalSpy failed(d.data(), &Discovery::failed);
        QVERIFY(failed.wait(2000));
    }

    void discoveryWaitsForReady()
    {
        auto* backend = new FakeBackend;
        backend->readyFlag = false;
        backend->list = { ups() };
        QPointer<Discovery> d = new SystemDiscovery(backend);
        QSignalSpy finished(d.data(), &Discovery::finished);
        QTimer::singleShot(100, backend, &FakeBackend::becomeReady);
        QVERIFY(finished.wait(2000));
        QCOMPARE(finished.first().at(0).value<QList<DiscoveredDevice>>().size(), 1);
    }

    void registration()
    {
        registerSystemSource();
        QVERIFY(SourceRegistry::hasFactory(SourceType::System));
        QVERIFY(Discovery::supports(SourceType::System));
    }

    // The real OS backend: whatever this machine has (often nothing in CI),
    // it must answer without hanging or crashing.
    void osBackendAnswers()
    {
        SystemPowerBackend* backend = SystemPowerBackend::create();
        std::unique_ptr<SystemPowerBackend> owner(backend);
        QVERIFY(!backend->backendName().isEmpty());
        QSignalSpy changed(backend, &SystemPowerBackend::changed);
        backend->refresh();
        QTRY_VERIFY_WITH_TIMEOUT(backend->isReady(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(changed.size() > 0, 10000);
        for (const auto& s : backend->sources()) {
            QVERIFY(!s.id.isEmpty());
            QVERIFY(!s.name.isEmpty());
            if (s.charge)
                QVERIFY(*s.charge >= 0 && *s.charge <= 100);
        }
    }
};

QTEST_GUILESS_MAIN(TestSystemPower)
#include "tst_systempower.moc"
