// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "common/FakeSource.h"
#include "core/engine/ActionArbiter.h"

#include <QTest>

using namespace nutty;
using namespace nutty::test;

class TestArbiter : public QObject
{
    Q_OBJECT

private:
    FakeClock clock;
    ActionSettings settings;  // margin 30 s, FSD delay 5 s
    QUuid device = QUuid::createUuid();

    RuleTrigger trigger(
        ActionType action, int countdownSec, std::optional<qint64> runtime = std::nullopt, bool fsd = false)
    {
        RuleTrigger t;
        t.rule = makeRule(device, fsd ? ConditionType::ForcedShutdown : ConditionType::ChargeBelow, 20,
            action, countdownSec);
        t.rule.name = enumKey(action);
        t.deviceId = device;
        t.deviceName = QStringLiteral("UPS-A");
        t.targetName = QStringLiteral("UPS-A");
        t.runtime = runtime;
        t.reason = QStringLiteral("because");
        t.episodeKey = QStringLiteral("d:x:1");
        t.fsd = fsd;
        return t;
    }

private slots:
    void countdownCap()
    {
        QCOMPARE(ActionArbiter::countdownFor(trigger(ActionType::Shutdown, 300), settings), 300);
        QCOMPARE(ActionArbiter::countdownFor(trigger(ActionType::Shutdown, 300, 100), settings), 70);
        QCOMPARE(ActionArbiter::countdownFor(trigger(ActionType::Shutdown, 60, 1000), settings), 60);
        QCOMPARE(ActionArbiter::countdownFor(trigger(ActionType::Shutdown, 300, 20), settings), 0);
        QCOMPARE(ActionArbiter::countdownFor(trigger(ActionType::Shutdown, -5), settings), 0);
        QCOMPARE(ActionArbiter::countdownFor(trigger(ActionType::Shutdown, 300, 1000, true), settings), 5);
    }

    void acceptAndTakeDue()
    {
        ActionArbiter arb(settings);
        const auto p = arb.makePending(trigger(ActionType::Shutdown, 60), clock.now);
        QCOMPARE(p.deadline, clock.now.addSecs(60));
        QVERIFY(p.cancellable);
        QCOMPARE(p.remainingSec(clock.now), 60);
        QCOMPARE(arb.offer(p, clock.now), ActionArbiter::Offer::Accepted);
        clock.advance(59);
        QVERIFY(!arb.takeDue(clock.now));
        clock.advance(1);
        const auto due = arb.takeDue(clock.now);
        QVERIFY(due);
        QCOMPARE(due->type, ActionType::Shutdown);
        QVERIFY(!arb.pending());
        QVERIFY(!arb.takeDue(clock.now));
    }

    void escalationKeepsEarlierDeadline()
    {
        ActionArbiter arb(settings);
        QCOMPARE(arb.offer(arb.makePending(trigger(ActionType::Sleep, 60), clock.now), clock.now),
            ActionArbiter::Offer::Accepted);
        clock.advance(10);
        // Stronger, later: the type escalates, the deadline stays.
        QCOMPARE(arb.offer(arb.makePending(trigger(ActionType::Shutdown, 120), clock.now), clock.now),
            ActionArbiter::Offer::Escalated);
        QCOMPARE(arb.pending()->type, ActionType::Shutdown);
        QCOMPARE(arb.pending()->ruleName, enumKey(ActionType::Shutdown));
        QCOMPARE(arb.pending()->deadline, clock.now.addSecs(50));
        QCOMPARE(arb.pending()->scheduledAt, clock.now.addSecs(-10));

        // Weaker: ignored.
        QCOMPARE(arb.offer(arb.makePending(trigger(ActionType::Command, 5), clock.now), clock.now),
            ActionArbiter::Offer::Ignored);
        QCOMPARE(arb.pending()->type, ActionType::Shutdown);
        // Same type, later: ignored.
        QCOMPARE(arb.offer(arb.makePending(trigger(ActionType::Shutdown, 300), clock.now), clock.now),
            ActionArbiter::Offer::Ignored);
        // Same type, earlier: the deadline moves up.
        QCOMPARE(arb.offer(arb.makePending(trigger(ActionType::Shutdown, 20), clock.now), clock.now),
            ActionArbiter::Offer::Escalated);
        QCOMPARE(arb.pending()->deadline, clock.now.addSecs(20));
    }

    void weakerButEarlierDoesNotDowngrade()
    {
        ActionArbiter arb(settings);
        arb.offer(arb.makePending(trigger(ActionType::Shutdown, 120), clock.now), clock.now);
        QCOMPARE(arb.offer(arb.makePending(trigger(ActionType::Sleep, 10), clock.now), clock.now),
            ActionArbiter::Offer::Ignored);
        QCOMPARE(arb.pending()->type, ActionType::Shutdown);
        QCOMPARE(arb.pending()->deadline, clock.now.addSecs(120));
    }

    void fsdEscalatesAndCannotBeCancelled()
    {
        ActionArbiter arb(settings);
        arb.offer(arb.makePending(trigger(ActionType::Shutdown, 120), clock.now), clock.now);
        QCOMPARE(arb.offer(arb.makePending(trigger(ActionType::Shutdown, 0, std::nullopt, true), clock.now),
                     clock.now),
            ActionArbiter::Offer::Escalated);
        QVERIFY(arb.pending()->fsd);
        QVERIFY(!arb.pending()->cancellable);
        QCOMPARE(arb.pending()->deadline, clock.now.addSecs(5));
        QVERIFY(!arb.cancel());
        QVERIFY(!arb.cancelIf([](const PendingAction&) { return true; }));
        QVERIFY(arb.pending());
    }

    void cancel()
    {
        ActionArbiter arb(settings);
        QVERIFY(!arb.cancel());
        arb.offer(arb.makePending(trigger(ActionType::Hibernate, 60), clock.now), clock.now);
        QVERIFY(!arb.cancelIf([](const PendingAction&) { return false; }));
        const auto c = arb.cancel();
        QVERIFY(c);
        QCOMPARE(c->type, ActionType::Hibernate);
        QVERIFY(!arb.pending());
        arb.offer(arb.makePending(trigger(ActionType::Hibernate, 60), clock.now), clock.now);
        QVERIFY(arb.cancelIf([](const PendingAction& p) { return p.episodeKey == u"d:x:1"; }));
    }

    void snooze()
    {
        ActionArbiter arb(settings);
        arb.snooze(clock.now.addSecs(300));
        QVERIFY(arb.isSnoozed(clock.now));
        QCOMPARE(arb.offer(arb.makePending(trigger(ActionType::Shutdown, 60), clock.now), clock.now),
            ActionArbiter::Offer::Snoozed);
        QVERIFY(!arb.pending());
        // FSD ignores a snooze.
        QCOMPARE(arb.offer(arb.makePending(trigger(ActionType::Shutdown, 0, std::nullopt, true), clock.now),
                     clock.now),
            ActionArbiter::Offer::Accepted);
        arb.clear();
        clock.advance(300);
        QVERIFY(!arb.isSnoozed(clock.now));
        QCOMPARE(arb.offer(arb.makePending(trigger(ActionType::Shutdown, 60), clock.now), clock.now),
            ActionArbiter::Offer::Accepted);
        arb.clear();
        arb.snooze(clock.now.addSecs(60));
        arb.clearSnooze();
        QVERIFY(!arb.isSnoozed(clock.now));
    }

    void simulationsNeverBlockRealActions()
    {
        ActionArbiter arb(settings);
        auto sim = arb.makePending(trigger(ActionType::Shutdown, 60), clock.now);
        sim.dryRun = true;
        QCOMPARE(arb.offer(sim, clock.now), ActionArbiter::Offer::Accepted);
        const auto real = arb.makePending(trigger(ActionType::Sleep, 90), clock.now);
        QCOMPARE(arb.offer(real, clock.now), ActionArbiter::Offer::Accepted);
        QVERIFY(!arb.pending()->dryRun);
        QCOMPARE(arb.pending()->type, ActionType::Sleep);
        QCOMPARE(arb.offer(sim, clock.now), ActionArbiter::Offer::Ignored);
        // Snoozes don't apply to simulations.
        arb.clear();
        arb.snooze(clock.now.addSecs(60));
        QCOMPARE(arb.offer(sim, clock.now), ActionArbiter::Offer::Accepted);
    }
};

QTEST_GUILESS_MAIN(TestArbiter)
#include "tst_arbiter.moc"
