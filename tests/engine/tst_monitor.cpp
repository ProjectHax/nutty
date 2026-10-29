// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "common/FakeSource.h"
#include "core/engine/Monitor.h"
#include "core/storage/HistoryStore.h"

#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <memory>

using namespace nutty;
using namespace nutty::test;

class TestMonitor : public QObject
{
    Q_OBJECT

private:
    FakeClock clock;
    std::unique_ptr<HistoryStore> history;
    std::unique_ptr<Monitor> monitor;
    AppConfig config;
    QUuid ups;
    QList<Event> events;
    QList<PendingAction> due;

    DeviceConfig& device() { return config.devices.first(); }
    FakeSource* fake() { return static_cast<FakeSource*>(monitor->source(ups)); }
    void push(DeviceStatus s)
    {
        s.sampledAt = clock.now;
        fake()->push(s);
    }
    // Reports `s` every 5 s for `seconds`, ticking every second like the app.
    void hold(const DeviceStatus& s, int seconds)
    {
        for (int t = 1; t <= seconds; ++t) {
            clock.advance(1);
            if (t % 5 == 0)
                push(s);
            monitor->tick();
        }
    }
    void idle(int seconds)
    {
        for (int t = 0; t < seconds; ++t) {
            clock.advance(1);
            monitor->tick();
        }
    }
    QList<EventType> eventTypes() const
    {
        QList<EventType> out;
        for (const auto& e : events)
            out << e.type;
        return out;
    }
    int count(EventType type) const { return static_cast<int>(eventTypes().count(type)); }
    Rule& addRule(ConditionType condition, double threshold, ActionType action, int countdownSec)
    {
        config.rules << makeRule(ups, condition, threshold, action, countdownSec);
        return config.rules.last();
    }
    void start() { monitor->applyConfig(config); }

private slots:
    void initTestCase() { FakeSource::registerFor(SourceType::Nut); }

    void init()
    {
        clock = {};
        events.clear();
        due.clear();
        history = std::make_unique<HistoryStore>();
        QVERIFY(history->open(QStringLiteral(":memory:")));
        monitor = std::make_unique<Monitor>(history.get());
        monitor->setAutoTick(false);
        monitor->setClock([this] { return clock.now; });
        connect(monitor.get(), &Monitor::eventRaised, this, [this](const Event& e) { events << e; });
        connect(monitor.get(), &Monitor::actionDue, this, [this](const PendingAction& p) { due << p; });

        config = {};
        config.devices << makeDevice(QStringLiteral("UPS-A"));
        device().notifyChargeBelow = 0;
        device().notifyRuntimeBelowSec = 0;
        ups = device().id;
    }

    void cleanup()
    {
        monitor.reset();
        history.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    void lowBatteryShutdownEndToEnd()
    {
        addRule(ConditionType::LowBattery, 0, ActionType::Shutdown, 60);
        QSignalSpy devicesChanged(monitor.get(), &Monitor::devicesChanged);
        QSignalSpy statusChanged(monitor.get(), &Monitor::deviceStatusChanged);
        QSignalSpy pendingChanged(monitor.get(), &Monitor::pendingActionChanged);
        start();
        QCOMPARE(devicesChanged.size(), 1);
        QCOMPARE(monitor->deviceIds(), QList { ups });
        QVERIFY(fake());
        QCOMPARE(fake()->starts, 1);
        QCOMPARE(monitor->health(ups), Monitor::Health::Unknown);

        push(online());
        QCOMPARE(statusChanged.size(), 1);
        QCOMPARE(monitor->health(ups), Monitor::Health::Ok);
        QCOMPARE(monitor->overallHealth(), Monitor::Health::Ok);
        QCOMPARE(monitor->connectionState(ups), ConnectionState::Connected);
        QCOMPARE(monitor->status(ups).charge, std::optional<double>(100));
        hold(online(), 5);

        push(onBattery(80, 1200));
        QCOMPARE(eventTypes(), QList { EventType::OnBattery });
        QCOMPARE(monitor->health(ups), Monitor::Health::OnBattery);
        QVERIFY(monitor->episodeStart(ups));
        const auto episodeId = monitor->episodeHistoryId(ups);
        QVERIFY(episodeId > 0);
        hold(onBattery(60, 900), 10);

        push(onBattery(30, 600, {}, StatusFlag::LowBattery));
        QCOMPARE(eventTypes(),
            (QList { EventType::OnBattery, EventType::LowBattery, EventType::ActionScheduled }));
        QCOMPARE(pendingChanged.size(), 1);
        const auto pending = monitor->pendingAction();
        QVERIFY(pending);
        QCOMPARE(pending->type, ActionType::Shutdown);
        QCOMPARE(pending->deviceId, ups);
        QCOMPARE(pending->deadline, clock.now.addSecs(60));
        QVERIFY(pending->cancellable);
        QVERIFY(!pending->dryRun);
        QCOMPARE(monitor->health(ups), Monitor::Health::Critical);
        QCOMPARE(monitor->overallHealth(), Monitor::Health::Critical);
        const auto& scheduled = events.last();
        QCOMPARE(scheduled.extra.value(QStringLiteral("action")).toString(), QStringLiteral("Shutdown"));
        QCOMPARE(scheduled.extra.value(QStringLiteral("countdownSec")).toLongLong(), 60LL);
        QVERIFY(scheduled.id > 0);

        hold(onBattery(29, 590, {}, StatusFlag::LowBattery), 59);
        QVERIFY(due.isEmpty());
        hold(onBattery(29, 580, {}, StatusFlag::LowBattery), 1);
        QCOMPARE(due.size(), 1);
        QCOMPARE(due.first().type, ActionType::Shutdown);
        QVERIFY(!monitor->pendingAction());
        hold(onBattery(28, 570, {}, StatusFlag::LowBattery), 30);
        QCOMPARE(due.size(), 1);  // once per episode

        monitor->reportActionResult(due.first(), true);
        QCOMPARE(events.last().type, EventType::ActionExecuted);
        monitor->prepareSourcesForShutdown();
        QCOMPARE(fake()->shutdownPreparations, 1);

        // Everything went to the history too.
        QCOMPARE(history->countEvents({}), static_cast<int>(events.size()));
        const auto episode = history->episode(episodeId);
        QVERIFY(episode);
        QVERIFY(episode->ongoing());
        QCOMPARE(episode->startCharge, std::optional<double>(80));
        QCOMPARE(episode->minCharge, std::optional<double>(28));
        QCOMPARE(episode->minRuntime, std::optional<qint64>(570));
        QVERIFY(episode->actions.size() >= 3);  // low battery, scheduled, started
    }

    void countdownCappedByRuntime()
    {
        addRule(ConditionType::LowBattery, 0, ActionType::Shutdown, 300);
        start();
        push(onBattery(10, 100, {}, StatusFlag::LowBattery));
        QVERIFY(monitor->pendingAction());
        QCOMPARE(monitor->pendingAction()->deadline, clock.now.addSecs(70));  // 100 - 30 s margin
    }

    void escalation()
    {
        addRule(ConditionType::OnBatteryFor, 0, ActionType::Sleep, 120);
        const auto low = addRule(ConditionType::LowBattery, 0, ActionType::Shutdown, 300).id;
        start();
        push(onBattery(80, 3000));
        QVERIFY(monitor->pendingAction());
        QCOMPARE(monitor->pendingAction()->type, ActionType::Sleep);
        const auto deadline = monitor->pendingAction()->deadline;
        hold(onBattery(70, 2900), 30);
        push(onBattery(20, 2000, {}, StatusFlag::LowBattery));
        const auto p = monitor->pendingAction();
        QVERIFY(p);
        QCOMPARE(p->type, ActionType::Shutdown);
        QCOMPARE(p->ruleId, low);
        QCOMPARE(p->deadline, deadline);  // the earlier one stays
        QCOMPARE(count(EventType::ActionScheduled), 2);
        hold(onBattery(20, 2000, {}, StatusFlag::LowBattery), 90);
        QCOMPARE(due.size(), 1);
        QCOMPARE(due.first().type, ActionType::Shutdown);
    }

    void autoCancelWhenPowerReturns()
    {
        addRule(ConditionType::ChargeBelow, 20, ActionType::Shutdown, 300);
        start();
        push(onBattery(15, 3000));
        QVERIFY(monitor->pendingAction());
        clock.advance(1);
        push(online(16));
        hold(online(16), 25);
        QVERIFY(monitor->pendingAction());  // not stable yet
        hold(online(17), 5);
        QVERIFY(!monitor->pendingAction());
        QCOMPARE(count(EventType::PowerRestored), 1);
        QCOMPARE(count(EventType::ActionCancelled), 1);
        QVERIFY(due.isEmpty());
        QCOMPARE(monitor->health(ups), Monitor::Health::Ok);
        const auto episodes = history->episodes(ups, {}, {});
        QCOMPARE(episodes.size(), 1);
        QVERIFY(!episodes.first().ongoing());
        QCOMPARE(episodes.first().durationSec(), 1);  // on battery until line power came back
        QCOMPARE(monitor->episodeHistoryId(ups), 0);
    }

    void userCancel()
    {
        addRule(ConditionType::LowBattery, 0, ActionType::Shutdown, 60);
        start();
        push(onBattery(20, 600, {}, StatusFlag::LowBattery));
        QVERIFY(monitor->pendingAction());
        QVERIFY(monitor->cancelPendingAction());
        QVERIFY(!monitor->pendingAction());
        QCOMPARE(events.last().type, EventType::ActionCancelled);
        QVERIFY(!monitor->cancelPendingAction());
        // The rule doesn't fire again during this outage.
        hold(onBattery(18, 500, {}, StatusFlag::LowBattery), 120);
        QVERIFY(!monitor->pendingAction());
        QVERIFY(due.isEmpty());
    }

    void snooze()
    {
        addRule(ConditionType::LowBattery, 0, ActionType::Shutdown, 60);
        start();
        push(onBattery(20, 3000, {}, StatusFlag::LowBattery));
        QVERIFY(monitor->pendingAction());
        QSignalSpy snoozeChanged(monitor.get(), &Monitor::snoozeChanged);
        monitor->snoozeActions(120);
        QCOMPARE(snoozeChanged.size(), 1);
        QVERIFY(!monitor->pendingAction());
        QCOMPARE(monitor->snoozedUntil(), clock.now.addSecs(120));
        hold(onBattery(19, 2900, {}, StatusFlag::LowBattery), 115);
        QVERIFY(!monitor->pendingAction());
        // Once the snooze is over the rule asks again.
        hold(onBattery(18, 2800, {}, StatusFlag::LowBattery), 10);
        QVERIFY(monitor->pendingAction());
        QVERIFY(!monitor->snoozedUntil().isValid());
        QVERIFY(due.isEmpty());
    }

    void forcedShutdownCannotBeStopped()
    {
        addRule(ConditionType::ForcedShutdown, 0, ActionType::Shutdown, 0);
        start();
        push(online());
        push(onBattery(10, 60, {}, StatusFlag::ForcedShutdown));
        auto p = monitor->pendingAction();
        QVERIFY(p);
        QVERIFY(p->fsd);
        QVERIFY(!p->cancellable);
        QCOMPARE(p->deadline, clock.now.addSecs(5));
        QVERIFY(!monitor->cancelPendingAction());
        monitor->snoozeActions(600);
        QVERIFY(monitor->pendingAction());
        idle(5);
        QCOMPARE(due.size(), 1);
        QVERIFY(due.first().fsd);
    }

    void dryRun()
    {
        addRule(ConditionType::LowBattery, 0, ActionType::Shutdown, 10);
        start();
        monitor->setDryRun(true);
        QVERIFY(monitor->dryRun());
        push(onBattery(20, 600, {}, StatusFlag::LowBattery));
        QVERIFY(monitor->pendingAction());
        QVERIFY(monitor->pendingAction()->dryRun);
        hold(onBattery(20, 600, {}, StatusFlag::LowBattery), 15);
        QVERIFY(due.isEmpty());
        QCOMPARE(events.last().type, EventType::ActionExecuted);
        QVERIFY(events.last().extra.value(QStringLiteral("dryRun")).toBool());
    }

    void simulateRule()
    {
        const auto rule = addRule(ConditionType::LowBattery, 0, ActionType::Hibernate, 30).id;
        start();
        push(online());
        QVERIFY(!monitor->simulateRule(QUuid::createUuid()));
        QVERIFY(monitor->simulateRule(rule));
        const auto p = monitor->pendingAction();
        QVERIFY(p);
        QVERIFY(p->dryRun);
        QCOMPARE(p->type, ActionType::Hibernate);
        QCOMPARE(monitor->health(ups), Monitor::Health::Ok);
        hold(online(), 30);
        QVERIFY(due.isEmpty());
        QVERIFY(!monitor->pendingAction());
        QCOMPARE(events.last().type, EventType::ActionExecuted);
        QVERIFY(events.last().extra.value(QStringLiteral("dryRun")).toBool());
    }

    void notifyRules()
    {
        addRule(ConditionType::ChargeBelow, 50, ActionType::Notify, 0);
        start();
        push(onBattery(40, 900));
        QVERIFY(!monitor->pendingAction());
        QCOMPARE(count(EventType::ActionExecuted), 1);
        QCOMPARE(events.last().extra.value(QStringLiteral("action")).toString(), QStringLiteral("Notify"));
        hold(onBattery(39, 800), 20);
        QCOMPARE(count(EventType::ActionExecuted), 1);
    }

    void resumeDropsCountdownAndWaitsForFreshData()
    {
        addRule(ConditionType::LowBattery, 0, ActionType::Shutdown, 60);
        start();
        push(onBattery(20, 600, {}, StatusFlag::LowBattery));
        QVERIFY(monitor->pendingAction());

        monitor->notifySleeping();
        clock.advance(3600);
        monitor->tick();
        QVERIFY(due.isEmpty());
        monitor->notifyResumed();
        QVERIFY(!monitor->pendingAction());
        QCOMPARE(events.last().type, EventType::ActionCancelled);
        QCOMPARE(fake()->resets, 1);
        QCOMPARE(monitor->wakeGraceUntil(), clock.now.addSecs(30));

        // Fresh data, but still in the grace period.
        idle(10);
        push(onBattery(19, 500, {}, StatusFlag::LowBattery));
        QVERIFY(!monitor->pendingAction());
        QCOMPARE(count(EventType::CommLost), 0);
        hold(onBattery(19, 500, {}, StatusFlag::LowBattery), 25);
        QVERIFY(monitor->pendingAction());
        QVERIFY(due.isEmpty());
    }

    void commLost()
    {
        start();
        push(online());
        idle(14);
        QVERIFY(!monitor->isCommLost(ups));
        QCOMPARE(monitor->health(ups), Monitor::Health::Ok);
        idle(1);
        QVERIFY(monitor->isCommLost(ups));
        QCOMPARE(count(EventType::CommLost), 1);
        QCOMPARE(monitor->health(ups), Monitor::Health::Offline);
        push(online());
        QCOMPARE(count(EventType::CommRestored), 1);
        QCOMPARE(monitor->health(ups), Monitor::Health::Ok);
    }

    void commLostOnBatteryIsCritical()
    {
        addRule(ConditionType::CommLostOnBattery, 0, ActionType::Shutdown, 60);
        start();
        push(onBattery(50, 1000));
        idle(15);
        QCOMPARE(count(EventType::CommLost), 1);
        QVERIFY(monitor->pendingAction());
        QCOMPARE(monitor->pendingAction()->type, ActionType::Shutdown);
    }

    void staleAndErrors()
    {
        // Poll 30 s: stale after 2.5 * 30 + 5 = 80 s, lost after 3 * 30 = 90 s.
        device().pollIntervalSec = 30;
        start();
        push(online());
        idle(80);
        QVERIFY(!monitor->isStale(ups));
        idle(1);
        QVERIFY(monitor->isStale(ups));
        QCOMPARE(monitor->connectionState(ups), ConnectionState::Stale);
        QCOMPARE(monitor->health(ups), Monitor::Health::Warning);
        QVERIFY(!monitor->isCommLost(ups));
        idle(9);
        QVERIFY(monitor->isCommLost(ups));
        QVERIFY(!monitor->isStale(ups));
        fake()->fail(SourceError::Auth, QStringLiteral("ERR ACCESS-DENIED"));
        QCOMPARE(monitor->lastErrorKind(ups), SourceError::Auth);
        QCOMPARE(monitor->lastError(ups), QStringLiteral("ERR ACCESS-DENIED"));
        QCOMPARE(monitor->connectionState(ups), ConnectionState::Error);
        QCOMPARE(count(EventType::AuthFailed), 1);
    }

    void samplesOncePerMinuteOnLinePower()
    {
        start();
        const auto t0 = clock.now;
        push(online());
        hold(online(), 120);
        QCOMPARE(history->samples(ups, {}, {}).size(), 3);  // 0, 60, 120 s
        clock.advance(1);
        push(onBattery(90, 1000));
        hold(onBattery(90, 1000), 20);
        QCOMPARE(history->samples(ups, t0.addSecs(121), {}).size(), 5);  // every poll on battery
    }

    void applyConfigOnlyRestartsChangedSources()
    {
        start();
        auto* first = fake();
        QVERIFY(first);
        device().name = QStringLiteral("Renamed");
        start();
        QCOMPARE(fake(), first);
        QCOMPARE(first->starts, 1);
        QCOMPARE(monitor->device(ups)->name, QStringLiteral("Renamed"));

        device().settings.insert(QStringLiteral("host"), QStringLiteral("192.0.2.5"));
        start();
        QVERIFY(fake() != first);
        QCOMPARE(first->stops, 1);
        QCOMPARE(fake()->starts, 1);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

        // Disabled: no source, unknown health, outage closed.
        push(onBattery(50, 900));
        const auto episodeId = monitor->episodeHistoryId(ups);
        QVERIFY(episodeId > 0);
        device().enabled = false;
        start();
        QVERIFY(!monitor->source(ups));
        QCOMPARE(monitor->health(ups), Monitor::Health::Unknown);
        QCOMPARE(monitor->overallHealth(), Monitor::Health::Unknown);
        QVERIFY(!history->episode(episodeId)->ongoing());

        device().enabled = true;
        start();
        QVERIFY(fake());
        monitor->pollAll();
        QCOMPARE(fake()->polls, 1);

        config.devices.clear();
        start();
        QVERIFY(monitor->deviceIds().isEmpty());
        QVERIFY(!monitor->source(ups));
    }

    void removingTheRuleCancelsItsCountdown()
    {
        addRule(ConditionType::LowBattery, 0, ActionType::Shutdown, 60);
        start();
        push(onBattery(20, 600, {}, StatusFlag::LowBattery));
        QVERIFY(monitor->pendingAction());
        config.rules.clear();
        start();
        QVERIFY(!monitor->pendingAction());
        QCOMPARE(events.last().type, EventType::ActionCancelled);
    }

    void unsupportedSourceType()
    {
        config.devices << makeDevice(QStringLiteral("Cloud"), SourceType::EcoFlow);
        const auto cloud = config.devices.last().id;
        start();
        QVERIFY(!monitor->source(cloud));
        QCOMPARE(monitor->lastErrorKind(cloud), SourceError::Config);
        QVERIFY(!monitor->lastError(cloud).isEmpty());
        QCOMPARE(monitor->health(cloud), Monitor::Health::Offline);
        idle(120);  // no CommLost storm for a device that can't run
        QCOMPARE(count(EventType::CommLost), 1);  // UPS-A, which never reported
    }

    void settingChangesAreLogged()
    {
        start();
        DeviceControl limit;
        limit.id = QStringLiteral("chargeLimit");
        limit.kind = ControlKind::Level;
        limit.minimum = 50;
        limit.unit = QStringLiteral("%");
        limit.value = 100;
        fake()->settings = { limit };
        push(online());

        QSignalSpy changed(monitor.get(), &Monitor::deviceControlsChanged);
        QSignalSpy finished(monitor.get(), &Monitor::controlFinished);
        QString error;
        QVERIFY(monitor->applyControl(ups, limit.id, 80, &error));
        QCOMPARE(fake()->applied.size(), 1);
        QCOMPARE(fake()->applied.first().second, 80.0);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(*monitor->controls(ups).first().pending, 80.0);

        fake()->finishControl(limit.id, 80, ControlResult::Applied);
        QCOMPARE(finished.size(), 1);
        QCOMPARE(count(EventType::SettingChanged), 1);
        const Event& done = events.last();
        QCOMPARE(done.severity, Severity::Info);
        QVERIFY2(done.title.contains(QStringLiteral("80")), qPrintable(done.title));
        QCOMPARE(done.extra.value(QStringLiteral("control")).toString(), limit.id);
        QCOMPARE(done.extra.value(QStringLiteral("result")).toString(), QStringLiteral("Applied"));
        QCOMPARE(history->countEvents({}), 1);

        fake()->finishControl(limit.id, 70, ControlResult::Unconfirmed);
        QCOMPARE(events.last().type, EventType::SettingFailed);
        QCOMPARE(events.last().severity, Severity::Warning);
        fake()->finishControl(limit.id, 60, ControlResult::Failed, QStringLiteral("rejected"));
        QCOMPARE(events.last().type, EventType::SettingFailed);
        QCOMPARE(events.last().message, QStringLiteral("rejected"));

        // A refusal comes back as an error, with nothing logged.
        fake()->refusal = QStringLiteral("busy");
        QVERIFY(!monitor->applyControl(ups, limit.id, 90, &error));
        QCOMPARE(error, QStringLiteral("busy"));
        QCOMPARE(count(EventType::SettingChanged) + count(EventType::SettingFailed), 3);

        // A paused device can't be changed.
        device().enabled = false;
        start();
        QVERIFY(!monitor->applyControl(ups, limit.id, 90, &error));
        QVERIFY(!error.isEmpty());
    }

    void worksWithoutHistory()
    {
        QList<PendingAction> local;
        Monitor m;
        m.setAutoTick(false);
        m.setClock([this] { return clock.now; });
        addRule(ConditionType::LowBattery, 0, ActionType::Shutdown, 0);
        m.applyConfig(config);
        connect(&m, &Monitor::actionDue, this, [&](const PendingAction& p) { local << p; });
        auto s = onBattery(5, 30, clock.now, StatusFlag::LowBattery);
        static_cast<FakeSource*>(m.source(ups))->push(s);
        QCOMPARE(local.size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestMonitor)
#include "tst_monitor.moc"
