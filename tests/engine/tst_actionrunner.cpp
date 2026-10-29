// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "common/FakeSource.h"
#include "core/engine/ActionRunner.h"
#include "core/engine/Monitor.h"
#include "core/storage/HistoryStore.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <memory>
#include <ranges>

using namespace nutty;
using namespace nutty::test;
using namespace std::chrono_literals;

// The power actions from the rule firing to the machine being told what to
// do: a Monitor with one fake UPS, an ActionRunner on top, and a backend that
// records what it was asked instead of touching the machine.
class TestActionRunner : public QObject
{
    Q_OBJECT

private:
    FakeClock clock;
    std::unique_ptr<HistoryStore> history;
    std::unique_ptr<Monitor> monitor;
    std::unique_ptr<ActionRunner> runner;
    AppConfig config;
    QUuid ups;
    QList<Event> events;
    // What the backend was asked, in order: "Sleep", "Shutdown", "Command: <text>".
    QStringList calls;
    // Actions the backend refuses, and the error it gives.
    QHash<ActionType, QString> failures;
    std::function<void()> onCall;
    // What reach() left the UPS reporting; keep() goes on reporting it. A UPS
    // that went silent (comm lost) stays silent.
    DeviceStatus current;
    bool silent = false;

    DeviceConfig& device() { return config.devices.first(); }
    FakeSource* fake() { return static_cast<FakeSource*>(monitor->source(ups)); }

    ActionRunner::Backend backend()
    {
        const auto answer = [this](ActionType type) -> ActionRunner::Result {
            if (onCall)
                onCall();
            if (failures.contains(type))
                return std::unexpected(failures.value(type));
            return {};
        };
        ActionRunner::Backend b;
        b.execute = [this, answer](ActionType type) {
            calls << enumKey(type);
            return answer(type);
        };
        b.runCommand = [this, answer](const QString& command) {
            calls << QStringLiteral("Command: ") + command;
            return answer(ActionType::Command);
        };
        return b;
    }

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
    void keep(int seconds)
    {
        if (silent)
            idle(seconds);
        else
            hold(current, seconds);
    }
    int count(EventType type) const
    {
        return static_cast<int>(std::ranges::count(events, type, &Event::type));
    }
    const Event* last(EventType type) const
    {
        const auto it = std::ranges::find(events | std::views::reverse, type, &Event::type);
        return it == (events | std::views::reverse).end() ? nullptr : &*it;
    }
    Rule& addRule(ConditionType condition, double threshold, ActionType action, int countdownSec)
    {
        config.rules << makeRule(ups, condition, threshold, action, countdownSec);
        return config.rules.last();
    }
    void start() { monitor->applyConfig(config); }

    // A threshold that each condition is tested with.
    static double thresholdFor(ConditionType condition)
    {
        switch (condition) {
        case ConditionType::OnBatteryFor:
            return 120;  // s
        case ConditionType::ChargeBelow:
            return 20;  // %
        case ConditionType::RuntimeBelow:
            return 600;  // s
        default:
            return 0;
        }
    }

    // Takes the UPS from line power to the moment `condition` holds, checking
    // on the way that nothing happens before it does.
    void reach(ConditionType condition)
    {
        push(online());
        hold(online(), 10);
        const auto nothingYet = [this](const char* when) {
            QVERIFY2(!monitor->pendingAction(), when);
            QVERIFY2(calls.isEmpty(), when);
            QVERIFY2(count(EventType::ActionScheduled) == 0, when);
            QVERIFY2(count(EventType::ActionExecuted) == 0, when);
        };

        switch (condition) {
        case ConditionType::OnBatteryFor:
            current = onBattery(85, 2900);
            push(onBattery(90, 3000));
            hold(current, 119);
            nothingYet("on battery for 119 s");
            if (QTest::currentTestFailed())
                return;
            hold(current, 1);
            break;
        case ConditionType::ChargeBelow:
            push(onBattery(50, 3000));
            hold(onBattery(21, 2000), 30);
            nothingYet("at 21 %");
            if (QTest::currentTestFailed())
                return;
            current = onBattery(20, 1800);
            push(current);
            break;
        case ConditionType::RuntimeBelow:
            push(onBattery(60, 1200));
            hold(onBattery(45, 601), 30);
            nothingYet("with 601 s left");
            if (QTest::currentTestFailed())
                return;
            current = onBattery(40, 600);
            push(current);
            break;
        case ConditionType::LowBattery:
            push(onBattery(40, 900));
            hold(onBattery(40, 900), 30);
            nothingYet("on battery, not low");
            if (QTest::currentTestFailed())
                return;
            current = onBattery(30, 800, {}, StatusFlag::LowBattery);
            push(current);
            break;
        case ConditionType::ForcedShutdown:
            push(onBattery(40, 900));
            hold(onBattery(40, 900), 30);
            nothingYet("on battery, no FSD");
            if (QTest::currentTestFailed())
                return;
            current = onBattery(30, 800, {}, StatusFlag::ForcedShutdown);
            push(current);
            break;
        case ConditionType::CommLostOnBattery:
            current = onBattery(50, 1000);
            push(current);
            hold(current, 10);
            idle(14);  // the 15 s dead time
            nothingYet("silent for 14 s");
            if (QTest::currentTestFailed())
                return;
            idle(1);
            QCOMPARE(count(EventType::CommLost), 1);
            silent = true;
            break;
        }
    }

private slots:
    void initTestCase() { FakeSource::registerFor(SourceType::Nut); }

    void init()
    {
        clock = {};
        events.clear();
        calls.clear();
        failures.clear();
        onCall = {};
        current = {};
        silent = false;
        history = std::make_unique<HistoryStore>();
        QVERIFY(history->open(QStringLiteral(":memory:")));
        monitor = std::make_unique<Monitor>(history.get());
        monitor->setAutoTick(false);
        monitor->setClock([this] { return clock.now; });
        connect(monitor.get(), &Monitor::eventRaised, this, [this](const Event& e) { events << e; });
        runner = std::make_unique<ActionRunner>(monitor.get(), history.get(), backend());

        config = {};
        config.devices << makeDevice(QStringLiteral("UPS-A"));
        device().notifyChargeBelow = 0;
        device().notifyRuntimeBelowSec = 0;
        ups = device().id;
    }

    void cleanup()
    {
        runner.reset();
        monitor.reset();
        history.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    // Every condition with every action: nothing before the condition holds,
    // nothing during the countdown, then exactly one call to the backend.
    void conditionRunsAction_data()
    {
        QTest::addColumn<ConditionType>("condition");
        QTest::addColumn<ActionType>("action");
        for (const auto condition : { ConditionType::OnBatteryFor, ConditionType::ChargeBelow,
                 ConditionType::RuntimeBelow, ConditionType::LowBattery, ConditionType::ForcedShutdown,
                 ConditionType::CommLostOnBattery }) {
            for (const auto action : { ActionType::Notify, ActionType::Command, ActionType::Sleep,
                     ActionType::Hibernate, ActionType::Shutdown }) {
                const auto tag = enumKey(condition) + u'/' + enumKey(action);
                QTest::newRow(qPrintable(tag)) << condition << action;
            }
        }
    }

    void conditionRunsAction()
    {
        QFETCH(ConditionType, condition);
        QFETCH(ActionType, action);
        auto& rule = addRule(condition, thresholdFor(condition), action, 60);
        rule.command = QStringLiteral("logger 'on battery'");
        start();
        reach(condition);
        if (QTest::currentTestFailed())
            return;

        if (action == ActionType::Notify) {
            // Nothing to run: the event is the action.
            QVERIFY(!monitor->pendingAction());
            QCOMPARE(count(EventType::ActionExecuted), 1);
            QCOMPARE(last(EventType::ActionExecuted)->extra.value(QStringLiteral("action")).toString(),
                QStringLiteral("Notify"));
            keep(120);
            QCOMPARE(count(EventType::ActionExecuted), 1);
            QVERIFY(calls.isEmpty());
            return;
        }

        // A forced shutdown gives the UPS server a few seconds, and can't be
        // stopped; everything else counts down for the rule's 60 s.
        const auto pending = monitor->pendingAction();
        QVERIFY(pending);
        QCOMPARE(pending->type, action);
        QCOMPARE(pending->ruleId, rule.id);
        const bool fsd = condition == ConditionType::ForcedShutdown;
        const int countdown = fsd ? config.actions.fsdDelaySec : 60;
        QCOMPARE(pending->deadline, clock.now.addSecs(countdown));
        QCOMPARE(pending->cancellable, !fsd);
        QCOMPARE(count(EventType::ActionScheduled), 1);

        keep(countdown - 1);
        QVERIFY(calls.isEmpty());
        QVERIFY(monitor->pendingAction());
        keep(1);
        QVERIFY(!monitor->pendingAction());
        const QString expected = action == ActionType::Command
            ? QStringLiteral("Command: logger 'on battery'")
            : enumKey(action);
        QCOMPARE(calls, QStringList { expected });

        const auto* executed = last(EventType::ActionExecuted);
        QVERIFY(executed);
        QCOMPARE(executed->extra.value(QStringLiteral("action")).toString(), enumKey(action));
        QVERIFY(!executed->extra.value(QStringLiteral("dryRun")).toBool());
        QCOMPARE(count(EventType::ActionFailed), 0);
        // Only a shutdown lets the sources say goodbye to the UPS server.
        QCOMPARE(fake()->shutdownPreparations, action == ActionType::Shutdown ? 1 : 0);

        // Once per outage.
        keep(180);
        QCOMPARE(calls.size(), 1);
    }

    void zeroCountdownActsAtOnce()
    {
        addRule(ConditionType::LowBattery, 0, ActionType::Hibernate, 0);
        start();
        reach(ConditionType::LowBattery);
        QCOMPARE(calls, QStringList { QStringLiteral("Hibernate") });
    }

    void failureIsReported()
    {
        failures.insert(ActionType::Shutdown, QStringLiteral("Not allowed by polkit"));
        addRule(ConditionType::LowBattery, 0, ActionType::Shutdown, 10);
        start();
        reach(ConditionType::LowBattery);
        keep(10);
        QCOMPARE(calls, QStringList { QStringLiteral("Shutdown") });
        QCOMPARE(count(EventType::ActionExecuted), 0);
        const auto* failed = last(EventType::ActionFailed);
        QVERIFY(failed);
        QCOMPARE(failed->severity, Severity::Critical);
        QCOMPARE(failed->extra.value(QStringLiteral("detail")).toString(),
            QStringLiteral("Not allowed by polkit"));
        QVERIFY(failed->message.contains(QStringLiteral("Not allowed by polkit")));
        // Still running, so the sources stay logged in.
        QCOMPARE(fake()->shutdownPreparations, 0);
        // And it doesn't try again in a loop.
        keep(120);
        QCOMPARE(calls.size(), 1);
    }

    void failedCommandIsReported()
    {
        failures.insert(ActionType::Command, QStringLiteral("No command is set."));
        addRule(ConditionType::ChargeBelow, 20, ActionType::Command, 0);
        start();
        reach(ConditionType::ChargeBelow);
        QCOMPARE(calls.size(), 1);
        QCOMPARE(last(EventType::ActionFailed)->extra.value(QStringLiteral("detail")).toString(),
            QStringLiteral("No command is set."));
    }

    void dryRunNeverCallsTheBackend()
    {
        addRule(ConditionType::LowBattery, 0, ActionType::Shutdown, 10);
        start();
        monitor->setDryRun(true);
        reach(ConditionType::LowBattery);
        keep(30);
        QVERIFY(calls.isEmpty());
        QCOMPARE(fake()->shutdownPreparations, 0);
        QVERIFY(last(EventType::ActionExecuted)->extra.value(QStringLiteral("dryRun")).toBool());

        // Even when handed a simulated action directly.
        monitor->setDryRun(false);
        PendingAction simulated;
        simulated.type = ActionType::Shutdown;
        simulated.deviceId = ups;
        simulated.dryRun = true;
        runner->run(simulated);
        QVERIFY(calls.isEmpty());
        QVERIFY(last(EventType::ActionExecuted)->extra.value(QStringLiteral("dryRun")).toBool());
    }

    void simulationNeverCallsTheBackend_data()
    {
        QTest::addColumn<ActionType>("action");
        QTest::newRow("Notify") << ActionType::Notify;
        QTest::newRow("Command") << ActionType::Command;
        QTest::newRow("Sleep") << ActionType::Sleep;
        QTest::newRow("Hibernate") << ActionType::Hibernate;
        QTest::newRow("Shutdown") << ActionType::Shutdown;
    }

    void simulationNeverCallsTheBackend()
    {
        QFETCH(ActionType, action);
        auto& rule = addRule(ConditionType::LowBattery, 0, action, 30);
        rule.command = QStringLiteral("true");
        start();
        push(online());
        QVERIFY(monitor->simulateRule(rule.id));
        hold(online(), 60);
        QVERIFY(calls.isEmpty());
        QCOMPARE(count(EventType::ActionExecuted), 1);
        QVERIFY(last(EventType::ActionExecuted)->extra.value(QStringLiteral("dryRun")).toBool());
    }

    void powerReturningCancels()
    {
        addRule(ConditionType::ChargeBelow, 20, ActionType::Shutdown, 60);
        start();
        reach(ConditionType::ChargeBelow);
        QVERIFY(monitor->pendingAction());
        hold(current, 10);
        push(online(21));
        hold(online(22), 30);  // line power has to hold for 30 s
        QVERIFY(!monitor->pendingAction());
        QCOMPARE(count(EventType::ActionCancelled), 1);
        hold(online(25), 120);
        QVERIFY(calls.isEmpty());
    }

    void userCancelStopsIt()
    {
        addRule(ConditionType::RuntimeBelow, 600, ActionType::Sleep, 60);
        start();
        reach(ConditionType::RuntimeBelow);
        keep(30);
        QVERIFY(monitor->cancelPendingAction());
        keep(120);
        QVERIFY(calls.isEmpty());
        QCOMPARE(count(EventType::ActionCancelled), 1);
    }

    void snoozeHoldsActionsBack()
    {
        addRule(ConditionType::ChargeBelow, 20, ActionType::Shutdown, 60);
        start();
        monitor->snoozeActions(3600);
        reach(ConditionType::ChargeBelow);
        keep(300);
        QVERIFY(calls.isEmpty());
    }

    void forcedShutdownIgnoresTheSnooze()
    {
        addRule(ConditionType::ForcedShutdown, 0, ActionType::Shutdown, 60);
        start();
        monitor->snoozeActions(3600);
        reach(ConditionType::ForcedShutdown);
        QVERIFY(!monitor->cancelPendingAction());
        keep(config.actions.fsdDelaySec);
        QCOMPARE(calls, QStringList { QStringLiteral("Shutdown") });
        QCOMPARE(fake()->shutdownPreparations, 1);
    }

    // A sleep that's counting down when the battery goes low becomes a
    // shutdown; only the shutdown runs.
    void escalationRunsOnlyTheStrongerAction()
    {
        addRule(ConditionType::OnBatteryFor, 0, ActionType::Sleep, 120);
        addRule(ConditionType::LowBattery, 0, ActionType::Shutdown, 300);
        start();
        push(online());
        push(onBattery(80, 3000));
        QCOMPARE(monitor->pendingAction()->type, ActionType::Sleep);
        hold(onBattery(70, 2900), 30);
        current = onBattery(20, 2000, {}, StatusFlag::LowBattery);
        push(current);
        QCOMPARE(monitor->pendingAction()->type, ActionType::Shutdown);
        keep(89);
        QVERIFY(calls.isEmpty());
        keep(1);
        QCOMPARE(calls, QStringList { QStringLiteral("Shutdown") });
        keep(300);
        QCOMPARE(calls.size(), 1);
    }

    // Each outage gets its own run of the rule.
    void nextOutageRunsAgain()
    {
        addRule(ConditionType::ChargeBelow, 20, ActionType::Command, 0).command = QStringLiteral("alert");
        start();
        reach(ConditionType::ChargeBelow);
        QCOMPARE(calls.size(), 1);
        push(online(25));
        hold(online(30), 40);
        push(onBattery(19, 1500));
        QCOMPARE(calls, (QStringList { QStringLiteral("Command: alert"), QStringLiteral("Command: alert") }));
    }

    // A brownout blip doesn't end the outage, so the rule doesn't run twice.
    void blipDoesNotRunItAgain()
    {
        addRule(ConditionType::ChargeBelow, 20, ActionType::Command, 0).command = QStringLiteral("alert");
        start();
        reach(ConditionType::ChargeBelow);
        push(online(20));
        hold(online(20), 10);
        push(onBattery(19, 1500));
        hold(onBattery(18, 1400), 60);
        QCOMPARE(calls.size(), 1);
    }

    void shutdownFallsBackToSleep_data()
    {
        QTest::addColumn<bool>("sleepWorks");
        QTest::newRow("sleep works") << true;
        QTest::newRow("sleep fails") << false;
    }

    void shutdownFallsBackToSleep()
    {
        QFETCH(bool, sleepWorks);
        if (!sleepWorks)
            failures.insert(ActionType::Sleep, QStringLiteral("no"));
        runner->setShutdownFallback(1s);
        addRule(ConditionType::LowBattery, 0, ActionType::Shutdown, 0);
        start();
        reach(ConditionType::LowBattery);
        QCOMPARE(calls, QStringList { QStringLiteral("Shutdown") });
        QCOMPARE(count(EventType::ActionExecuted), 1);

        QTRY_COMPARE(calls, (QStringList { QStringLiteral("Shutdown"), QStringLiteral("Sleep") }));
        const auto* failed = last(EventType::ActionFailed);
        QVERIFY(failed);
        QCOMPARE(failed->extra.value(QStringLiteral("detail")).toString(),
            sleepWorks
                ? QStringLiteral(
                      "The computer did not shut down within 1 s, so nutty put it to sleep instead")
                : QStringLiteral("The computer did not shut down within 1 s, and sleep failed too: no"));
    }

    void noFallbackWhenTheShutdownFailed()
    {
        failures.insert(ActionType::Shutdown, QStringLiteral("refused"));
        runner->setShutdownFallback(20ms);
        addRule(ConditionType::LowBattery, 0, ActionType::Shutdown, 0);
        start();
        reach(ConditionType::LowBattery);
        QTest::qWait(100);
        QCOMPARE(calls, QStringList { QStringLiteral("Shutdown") });
    }

    void noFallbackAfterSleep()
    {
        runner->setShutdownFallback(20ms);
        addRule(ConditionType::LowBattery, 0, ActionType::Sleep, 0);
        start();
        reach(ConditionType::LowBattery);
        QTest::qWait(100);
        QCOMPARE(calls, QStringList { QStringLiteral("Sleep") });
    }

    // The outage so far is written out before the machine goes down: samples
    // are otherwise buffered for a while.
    void historyIsOnDiskBeforeTheActionRuns()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("history.db"));
        runner.reset();
        monitor.reset();
        history = std::make_unique<HistoryStore>();
        QVERIFY(history->open(path));
        monitor = std::make_unique<Monitor>(history.get());
        monitor->setAutoTick(false);
        monitor->setClock([this] { return clock.now; });
        runner = std::make_unique<ActionRunner>(monitor.get(), history.get(), backend());

        const auto samplesOnDisk = [&] {
            int rows = -1;
            {
                auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("probe"));
                db.setDatabaseName(path);
                db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
                if (db.open()) {
                    QSqlQuery q(QStringLiteral("SELECT COUNT(*) FROM samples"), db);
                    if (q.next())
                        rows = q.value(0).toInt();
                }
            }
            QSqlDatabase::removeDatabase(QStringLiteral("probe"));
            return rows;
        };
        int atAction = -1;
        onCall = [&] { atAction = samplesOnDisk(); };

        addRule(ConditionType::LowBattery, 0, ActionType::Shutdown, 20);
        start();
        reach(ConditionType::LowBattery);
        keep(19);
        const int before = samplesOnDisk();
        keep(1);
        QCOMPARE(calls, QStringList { QStringLiteral("Shutdown") });
        // Still buffered a second earlier, or this proves nothing.
        QVERIFY2(before >= 0 && before + 2 < atAction,
            qPrintable(QStringLiteral("%1 then %2").arg(before).arg(atAction)));
        QCOMPARE(atAction, static_cast<int>(history->samples(ups, {}, {}).size()));
    }
};

QTEST_GUILESS_MAIN(TestActionRunner)
#include "tst_actionrunner.moc"
