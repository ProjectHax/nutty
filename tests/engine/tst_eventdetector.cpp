// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "common/FakeSource.h"
#include "core/engine/EventDetector.h"

#include <QTest>

#include <algorithm>

using namespace nutty;
using namespace nutty::test;

namespace {

QList<EventType> types(const QList<Event>& events)
{
    QList<EventType> out;
    for (const auto& e : events)
        out << e.type;
    return out;
}

}  // namespace

class TestEventDetector : public QObject
{
    Q_OBJECT

private:
    FakeClock clock;
    DeviceConfig dev = makeDevice(QStringLiteral("UPS-A"));
    EventDetector det;

    QList<Event> status(const DeviceStatus& s) { return det.onStatus(dev, s, clock.now); }
    QList<Event> tick() { return det.tick(dev, clock.now); }
    // Keeps reporting `s` every 5 s for `seconds`, ticking in between.
    QList<Event> hold(const DeviceStatus& s, int seconds)
    {
        QList<Event> out;
        for (int t = 0; t < seconds; t += 5) {
            clock.advance(std::min(5, seconds - t));
            out << status(s) << tick();
        }
        return out;
    }

private slots:
    void init()
    {
        clock = {};
        dev = makeDevice(QStringLiteral("UPS-A"));
        dev.notifyChargeBelow = 0;
        dev.notifyRuntimeBelowSec = 0;
        det = EventDetector(ActionSettings {});
    }

    void onBatteryOncePerEpisode()
    {
        QVERIFY(status(online()).isEmpty());
        auto ev = status(onBattery(90, 1200, clock.now));
        QCOMPARE(types(ev), QList { EventType::OnBattery });
        QVERIFY(ev.first().message.contains(QStringLiteral("UPS-A")));
        QCOMPARE(ev.first().charge, std::optional<double>(90));
        QCOMPARE(det.state(dev.id)->episodeSerial, 1);

        clock.advance(5);
        QVERIFY(status(onBattery(88, 1100)).isEmpty());

        // A flicker back to line power and off again is still the same outage.
        clock.advance(5);
        QVERIFY(status(online(88)).isEmpty());
        QVERIFY(!det.state(dev.id)->onBatteryNow());
        clock.advance(5);
        QVERIFY(status(onBattery(87, 1000)).isEmpty());
        QVERIFY(det.state(dev.id)->onBatteryNow());
        QCOMPARE(det.state(dev.id)->episodeSerial, 1);

        // Line power must hold for stableOnlineSec (30 s).
        clock.advance(5);
        QVERIFY(status(online(87)).isEmpty());
        QVERIFY(hold(online(88), 25).isEmpty());
        QVERIFY(det.state(dev.id)->inEpisode);
        QCOMPARE(types(hold(online(88), 5)), QList { EventType::PowerRestored });
        QVERIFY(!det.state(dev.id)->inEpisode);

        clock.advance(60);
        ev = status(onBattery(80, 900));
        QCOMPARE(types(ev), QList { EventType::OnBattery });
        QCOMPARE(det.state(dev.id)->episodeSerial, 2);
    }

    void powerRestoredReportsOutageLength()
    {
        status(onBattery(90, 1200, clock.now));
        QVERIFY(hold(onBattery(85, 1100), 100).isEmpty());
        status(online(80));
        const auto ev = hold(online(80), 30);
        QCOMPARE(types(ev), QList { EventType::PowerRestored });
        QCOMPARE(ev.first().extra.value(QStringLiteral("onBatterySec")).toLongLong(), 100LL);
        QVERIFY(!det.state(dev.id)->inEpisode);
        QVERIFY(tick().isEmpty());
    }

    void lowBatteryLatchedForEpisode()
    {
        auto ev = status(onBattery(30, 300, {}, StatusFlag::LowBattery));
        QCOMPARE(types(ev), (QList { EventType::OnBattery, EventType::LowBattery }));
        clock.advance(5);
        QVERIFY(status(onBattery(31, 310)).isEmpty());  // LB flickers off
        QVERIFY(det.state(dev.id)->lowBatteryLatched);
        clock.advance(5);
        QVERIFY(status(onBattery(29, 290, {}, StatusFlag::LowBattery)).isEmpty());

        // Back on line, LB still set while recharging: no new LB.
        clock.advance(5);
        const auto recharging = makeStatus(StatusFlags(StatusFlag::Online) | StatusFlag::LowBattery, 30, 300);
        QVERIFY(status(recharging).isEmpty());
        QCOMPARE(types(hold(recharging, 30)), QList { EventType::PowerRestored });
        clock.advance(5);
        QVERIFY(
            status(makeStatus(StatusFlags(StatusFlag::Online) | StatusFlag::LowBattery, 35, 350)).isEmpty());

        // A new outage announces LB again.
        clock.advance(5);
        ev = status(onBattery(34, 340, {}, StatusFlag::LowBattery));
        QCOMPARE(types(ev), (QList { EventType::OnBattery, EventType::LowBattery }));
    }

    void thresholdsOncePerEpisode()
    {
        dev.notifyChargeBelow = 50;
        dev.notifyRuntimeBelowSec = 300;
        QCOMPARE(types(status(onBattery(60, 900))), QList { EventType::OnBattery });
        clock.advance(5);
        QCOMPARE(types(status(onBattery(50, 600))), QList { EventType::ChargeBelow });
        clock.advance(5);
        QCOMPARE(types(status(onBattery(45, 300))), QList { EventType::RuntimeBelow });
        clock.advance(5);
        QVERIFY(status(onBattery(40, 200)).isEmpty());

        // Not while on line power inside the episode either.
        clock.advance(5);
        QVERIFY(status(online(40, 200)).isEmpty());
        QCOMPARE(types(hold(online(40, 200), 30)), QList { EventType::PowerRestored });
        clock.advance(5);
        QCOMPARE(types(status(onBattery(40, 200))),
            (QList { EventType::OnBattery, EventType::ChargeBelow, EventType::RuntimeBelow }));
    }

    void risingEdges()
    {
        status(online());
        clock.advance(5);
        auto ev = status(makeStatus(StatusFlags(StatusFlag::Online) | StatusFlag::ReplaceBattery
            | StatusFlag::Overload | StatusFlag::Bypass | StatusFlag::Off | StatusFlag::Alarm));
        QCOMPARE(types(ev),
            (QList { EventType::ReplaceBattery, EventType::Overload, EventType::Bypass, EventType::UpsOff,
                EventType::Alarm }));
        clock.advance(5);
        ev = status(makeStatus(StatusFlags(StatusFlag::Online) | StatusFlag::ReplaceBattery
            | StatusFlag::Overload | StatusFlag::Bypass | StatusFlag::Off | StatusFlag::Alarm));
        QVERIFY(ev.isEmpty());
        clock.advance(5);
        QVERIFY(status(online()).isEmpty());
        clock.advance(5);
        QCOMPARE(types(status(makeStatus(StatusFlags(StatusFlag::Online) | StatusFlag::Overload))),
            QList { EventType::Overload });
    }

    void alarmTextChange()
    {
        auto s = online();
        s.alarmText = QStringLiteral("Replace battery!");
        auto ev = status(s);
        QCOMPARE(types(ev), QList { EventType::Alarm });
        QCOMPARE(
            ev.first().extra.value(QStringLiteral("alarm")).toString(), QStringLiteral("Replace battery!"));
        QVERIFY(status(s).isEmpty());
        s.alarmText = QStringLiteral("Temperature high");
        QCOMPARE(types(status(s)), QList { EventType::Alarm });
        s.alarmText.clear();
        QVERIFY(status(s).isEmpty());
    }

    void forcedShutdownSerial()
    {
        status(online());
        QCOMPARE(det.state(dev.id)->fsdSerial, 0);
        auto ev = status(onBattery(20, 100, {}, StatusFlag::ForcedShutdown));
        QVERIFY(types(ev).contains(EventType::ForcedShutdown));
        QCOMPARE(det.state(dev.id)->fsdSerial, 1);
        QVERIFY(det.state(dev.id)->fsdActive());
        QVERIFY(!types(status(onBattery(20, 100, {}, StatusFlag::ForcedShutdown)))
                .contains(EventType::ForcedShutdown));
        status(onBattery(20, 100));
        QVERIFY(types(status(onBattery(20, 100, {}, StatusFlag::ForcedShutdown)))
                .contains(EventType::ForcedShutdown));
        QCOMPARE(det.state(dev.id)->fsdSerial, 2);
    }

    void commLostAfterDeadtime()
    {
        // Poll 5 s: deadtime = max(15, 3 * 5) = 15 s.
        QCOMPARE(det.deadTimeSec(dev), 15);
        status(online());
        clock.advance(14);
        QVERIFY(tick().isEmpty());
        clock.advance(1);
        det.onError(dev, SourceError::Network, QStringLiteral("connection refused"), clock.now);
        auto ev = tick();
        QCOMPARE(types(ev), QList { EventType::CommLost });
        QVERIFY(ev.first().message.contains(QStringLiteral("connection refused")));
        QVERIFY(det.state(dev.id)->commLost);
        clock.advance(10);
        QVERIFY(tick().isEmpty());
        ev = status(online());
        QCOMPARE(types(ev), QList { EventType::CommRestored });
        QVERIFY(!det.state(dev.id)->commLost);
    }

    void commLostWithoutAnyData()
    {
        QVERIFY(tick().isEmpty());  // starts tracking
        dev.pollIntervalSec = 10;  // deadtime 30 s
        clock.advance(29);
        QVERIFY(tick().isEmpty());
        clock.advance(1);
        QCOMPARE(types(tick()), QList { EventType::CommLost });
    }

    void episodeDoesNotEndWithoutContact()
    {
        status(onBattery(80, 1000));
        clock.advance(5);
        status(online(80));
        clock.advance(15);
        QCOMPARE(types(tick()), QList { EventType::CommLost });
        clock.advance(30);
        QVERIFY(tick().isEmpty());
        QVERIFY(det.state(dev.id)->inEpisode);
        // Contact is back: the stable period starts over from here.
        auto ev = status(online(80));
        QCOMPARE(types(ev), QList { EventType::CommRestored });
        clock.advance(29);
        status(online(81));
        QVERIFY(det.state(dev.id)->inEpisode);
        clock.advance(1);
        QCOMPARE(types(status(online(81))), QList { EventType::PowerRestored });
    }

    void authFailedOnceUntilSuccess()
    {
        auto ev = det.onError(dev, SourceError::Auth, QStringLiteral("ERR ACCESS-DENIED"), clock.now);
        QCOMPARE(types(ev), QList { EventType::AuthFailed });
        QVERIFY(
            det.onError(dev, SourceError::Auth, QStringLiteral("ERR ACCESS-DENIED"), clock.now).isEmpty());
        QVERIFY(det.onError(dev, SourceError::Network, QStringLiteral("timeout"), clock.now).isEmpty());
        status(online());
        QCOMPARE(types(det.onError(dev, SourceError::Auth, {}, clock.now)), QList { EventType::AuthFailed });
    }

    void resumeNeedsFreshData()
    {
        status(onBattery(80, 1000));
        clock.advance(5);
        status(online(80));
        clock.advance(10);
        det.markResumed(clock.now);
        const auto* s = det.state(dev.id);
        QVERIFY(s->awaitingFreshData);
        QVERIFY(!s->onlineSince.isValid());
        // Without data the episode can't end, however long it is.
        clock.advance(10);
        QVERIFY(tick().isEmpty());
        status(online(80));
        QVERIFY(!det.state(dev.id)->awaitingFreshData);
        clock.advance(30);
        QCOMPARE(types(status(online(81))), QList { EventType::PowerRestored });
    }

    void estimatedRuntime()
    {
        status(onBattery(50, 600));
        clock.advance(100);
        QCOMPARE(det.state(dev.id)->estimatedRuntime(clock.now), std::optional<qint64>(500));
        clock.advance(1000);
        QCOMPARE(det.state(dev.id)->estimatedRuntime(clock.now), std::optional<qint64>(0));
    }

    void eventsAreTranslatedSnapshots()
    {
        const auto ev = status(onBattery(42, 1500, {}, StatusFlag::LowBattery));
        for (const auto& e : ev) {
            QCOMPARE(e.deviceId, dev.id);
            QCOMPARE(e.deviceName, QStringLiteral("UPS-A"));
            QCOMPARE(e.timestamp, clock.now);
            QVERIFY(!e.title.isEmpty());
            QVERIFY(e.flags.testFlag(StatusFlag::LowBattery));
        }
        QCOMPARE(ev.last().severity, Severity::Critical);
    }
};

QTEST_GUILESS_MAIN(TestEventDetector)
#include "tst_eventdetector.moc"
