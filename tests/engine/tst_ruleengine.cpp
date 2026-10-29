// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "common/FakeSource.h"
#include "core/engine/EventDetector.h"
#include "core/engine/RuleEngine.h"

#include <QTest>

#include <algorithm>

using namespace nutty;
using namespace nutty::test;

class TestRuleEngine : public QObject
{
    Q_OBJECT

private:
    FakeClock clock;
    AppConfig config;
    EventDetector det;
    RuleEngine rules;
    QDateTime grace;

    QUuid addDevice(const QString& name)
    {
        config.devices << makeDevice(name);
        return config.devices.last().id;
    }
    DeviceConfig& dev(const QUuid& id) { return *std::ranges::find(config.devices, id, &DeviceConfig::id); }
    Rule& addRule(const Rule& r)
    {
        config.rules << r;
        return config.rules.last();
    }
    void push(const QUuid& id, const DeviceStatus& s) { det.onStatus(*config.device(id), s, clock.now); }
    // Advances time in 5 s steps, every device reporting `s`.
    void hold(const QUuid& id, const DeviceStatus& s, int seconds)
    {
        for (int t = 0; t < seconds; t += 5) {
            clock.advance(std::min(5, seconds - t));
            push(id, s);
            for (const auto& d : std::as_const(config.devices))
                det.tick(d, clock.now);
        }
    }
    QList<RuleTrigger> evaluate() { return rules.evaluate(config, det.states(), clock.now, grace); }
    // Evaluates and marks everything fired, as the Monitor does.
    QList<RuleTrigger> fire()
    {
        const auto triggers = evaluate();
        for (const auto& t : triggers)
            rules.markFired(t.rule.id, t.episodeKey);
        return triggers;
    }

private slots:
    void init()
    {
        clock = {};
        config = {};
        det = EventDetector(config.actions);
        rules = RuleEngine {};
        grace = {};
    }

    void oneShotPerEpisode()
    {
        const auto a = addDevice(QStringLiteral("A"));
        const auto rule = addRule(makeRule(a, ConditionType::ChargeBelow, 20, ActionType::Shutdown)).id;
        push(a, online());
        QVERIFY(fire().isEmpty());
        push(a, onBattery(50, 900));
        QVERIFY(fire().isEmpty());
        clock.advance(5);
        push(a, onBattery(18, 300));
        const auto t = fire();
        QCOMPARE(t.size(), 1);
        QCOMPARE(t.first().rule.id, rule);
        QCOMPARE(t.first().deviceId, a);
        QCOMPARE(t.first().runtime, std::optional<qint64>(300));
        QVERIFY(!t.first().reason.isEmpty());
        QVERIFY(!t.first().fsd);

        clock.advance(5);
        push(a, onBattery(15, 250));
        QVERIFY(fire().isEmpty());

        // Unmarking (as after a resume) lets it fire again in this episode.
        QCOMPARE(evaluate().size(), 0);
        rules.unmarkFired(rule);
        QCOMPARE(evaluate().size(), 1);
    }

    void episodeEndsOnlyAfterStableLine()
    {
        const auto a = addDevice(QStringLiteral("A"));
        addRule(makeRule(a, ConditionType::ChargeBelow, 20, ActionType::Shutdown));
        push(a, onBattery(18, 300));
        QCOMPARE(fire().size(), 1);

        // A short return of line power doesn't re-arm the rule.
        hold(a, online(19), 10);
        hold(a, onBattery(17, 280), 5);
        QVERIFY(fire().isEmpty());

        // Stable line power ends the outage; the next one may fire again.
        hold(a, online(19), 30);  // line power seen since the first of these reports: 25 s
        QVERIFY(det.state(a)->inEpisode);
        hold(a, online(19), 5);
        QVERIFY(!det.state(a)->inEpisode);
        QVERIFY(fire().isEmpty());
        hold(a, onBattery(16, 260), 5);
        QCOMPARE(fire().size(), 1);
    }

    void onBatteryFor()
    {
        const auto a = addDevice(QStringLiteral("A"));
        addRule(makeRule(a, ConditionType::OnBatteryFor, 60, ActionType::Hibernate));
        push(a, onBattery(90, 3000));
        hold(a, onBattery(90, 3000), 55);
        QVERIFY(fire().isEmpty());
        hold(a, onBattery(89, 2900), 5);
        QCOMPARE(fire().size(), 1);
    }

    void runtimeBelowAndLowBattery()
    {
        const auto a = addDevice(QStringLiteral("A"));
        const auto runtime = addRule(makeRule(a, ConditionType::RuntimeBelow, 120, ActionType::Sleep)).id;
        const auto low = addRule(makeRule(a, ConditionType::LowBattery, 0, ActionType::Shutdown)).id;
        push(a, onBattery(40, 300));
        QVERIFY(fire().isEmpty());
        hold(a, onBattery(30, 120), 5);
        auto t = fire();
        QCOMPARE(t.size(), 1);
        QCOMPARE(t.first().rule.id, runtime);
        hold(a, onBattery(25, 100, {}, StatusFlag::LowBattery), 5);
        // LB stays latched even when the flag flickers.
        hold(a, onBattery(26, 110), 5);
        t = fire();
        QCOMPARE(t.size(), 1);
        QCOMPARE(t.first().rule.id, low);
    }

    void commLostOnBattery()
    {
        const auto a = addDevice(QStringLiteral("A"));
        addRule(makeRule(a, ConditionType::CommLostOnBattery, 120, ActionType::Shutdown));
        push(a, onBattery(60, 400));
        // Deadtime is 15 s; then the runtime estimate counts down from 400.
        clock.advance(15);
        det.tick(dev(a), clock.now);
        QVERIFY(det.state(a)->commLost);
        QVERIFY(fire().isEmpty());  // ~385 s estimated left
        clock.advance(400 - 15 - 120 - 1);
        QVERIFY(fire().isEmpty());
        clock.advance(1);
        const auto t = fire();
        QCOMPARE(t.size(), 1);
        QCOMPARE(t.first().runtime, std::optional<qint64>(120));
    }

    void commLostWithoutRuntimeFiresAtDeadtime()
    {
        const auto a = addDevice(QStringLiteral("A"));
        addRule(makeRule(a, ConditionType::CommLostOnBattery, 120, ActionType::Shutdown));
        push(a, onBattery(60, std::nullopt));
        clock.advance(14);
        det.tick(dev(a), clock.now);
        QVERIFY(fire().isEmpty());
        clock.advance(1);
        det.tick(dev(a), clock.now);
        QCOMPARE(fire().size(), 1);
    }

    void commLostNotCritical()
    {
        const auto a = addDevice(QStringLiteral("A"));
        dev(a).commLostCritical = false;
        addRule(makeRule(a, ConditionType::CommLostOnBattery, 0, ActionType::Shutdown));
        push(a, onBattery(60, 100));
        clock.advance(20);
        det.tick(dev(a), clock.now);
        QVERIFY(det.state(a)->commLost);
        QVERIFY(fire().isEmpty());
    }

    void commLostOnLinePowerIsNotCritical()
    {
        const auto a = addDevice(QStringLiteral("A"));
        addRule(makeRule(a, ConditionType::CommLostOnBattery, 0, ActionType::Shutdown));
        push(a, online());
        clock.advance(20);
        det.tick(dev(a), clock.now);
        QVERIFY(det.state(a)->commLost);
        QVERIFY(fire().isEmpty());
    }

    void forcedShutdown()
    {
        const auto a = addDevice(QStringLiteral("A"));
        addRule(makeRule(a, ConditionType::ForcedShutdown, 0, ActionType::Shutdown, 0));
        push(a, online());
        QVERIFY(fire().isEmpty());
        push(a, onBattery(10, 60, {}, StatusFlag::ForcedShutdown));
        auto t = fire();
        QCOMPARE(t.size(), 1);
        QVERIFY(t.first().fsd);
        QVERIFY(t.first().episodeKey.startsWith(u"f:"));
        QVERIFY(rules.isEpisodeActive(t.first().episodeKey, det.states()));
        clock.advance(5);
        push(a, onBattery(10, 60, {}, StatusFlag::ForcedShutdown));
        QVERIFY(fire().isEmpty());
        // A new FSD (flag dropped and raised again) fires again.
        push(a, onBattery(10, 60));
        push(a, onBattery(10, 60, {}, StatusFlag::ForcedShutdown));
        QCOMPARE(fire().size(), 1);
    }

    void wakeGrace()
    {
        const auto a = addDevice(QStringLiteral("A"));
        addRule(makeRule(a, ConditionType::LowBattery, 0, ActionType::Shutdown));
        addRule(makeRule(a, ConditionType::ForcedShutdown, 0, ActionType::Shutdown, 0));
        push(a, onBattery(20, 100, {}, StatusFlag::LowBattery));
        det.markResumed(clock.now);
        grace = clock.now.addSecs(config.actions.wakeGraceSec);

        // Stale pre-sleep data: nothing fires.
        QVERIFY(fire().isEmpty());
        clock.advance(10);
        push(a, onBattery(20, 100, {}, StatusFlag::LowBattery));
        QVERIFY(fire().isEmpty());  // fresh, but still in the grace period

        // FSD is acted on as soon as it is reported again, grace or not.
        clock.advance(5);
        push(a, onBattery(20, 100, {}, StatusFlags(StatusFlag::LowBattery) | StatusFlag::ForcedShutdown));
        auto t = fire();
        QCOMPARE(t.size(), 1);
        QVERIFY(t.first().fsd);

        clock.advance(15);
        t = fire();
        QCOMPARE(t.size(), 1);
        QCOMPARE(t.first().rule.condition, ConditionType::LowBattery);
    }

    void wakeGraceWaitsForFreshData()
    {
        const auto a = addDevice(QStringLiteral("A"));
        addRule(makeRule(a, ConditionType::LowBattery, 0, ActionType::Shutdown));
        push(a, onBattery(20, 100, {}, StatusFlag::LowBattery));
        det.markResumed(clock.now);
        grace = clock.now.addSecs(config.actions.wakeGraceSec);
        clock.advance(40);  // grace over, but nothing new has arrived
        QVERIFY(fire().isEmpty());
        push(a, onBattery(20, 100, {}, StatusFlag::LowBattery));
        QCOMPARE(fire().size(), 1);
    }

    void silentAfterResumeCountsAsLost()
    {
        const auto a = addDevice(QStringLiteral("A"));
        addRule(makeRule(a, ConditionType::CommLostOnBattery, 0, ActionType::Shutdown));
        push(a, onBattery(50, 600));
        det.markResumed(clock.now);
        grace = clock.now.addSecs(config.actions.wakeGraceSec);
        clock.advance(31);
        det.tick(dev(a), clock.now);
        QVERIFY(det.state(a)->commLost);
        QCOMPARE(fire().size(), 1);
    }

    void perDeviceIndependence()
    {
        const auto a = addDevice(QStringLiteral("A"));
        const auto b = addDevice(QStringLiteral("B"));
        const auto ra = addRule(makeRule(a, ConditionType::ChargeBelow, 20, ActionType::Shutdown)).id;
        const auto rb = addRule(makeRule(b, ConditionType::ChargeBelow, 20, ActionType::Sleep)).id;
        push(a, onBattery(15, 100));
        push(b, online());
        auto t = fire();
        QCOMPARE(t.size(), 1);
        QCOMPARE(t.first().rule.id, ra);
        push(b, onBattery(19, 200));
        t = fire();
        QCOMPARE(t.size(), 1);
        QCOMPARE(t.first().rule.id, rb);
        QCOMPARE(t.first().deviceName, QStringLiteral("B"));
    }

    void disabledDevicesAndRules()
    {
        const auto a = addDevice(QStringLiteral("A"));
        auto& r = addRule(makeRule(a, ConditionType::ChargeBelow, 20, ActionType::Shutdown));
        push(a, onBattery(15, 100));
        r.enabled = false;
        QVERIFY(evaluate().isEmpty());
        r.enabled = true;
        dev(a).enabled = false;
        QVERIFY(evaluate().isEmpty());
        dev(a).enabled = true;
        QCOMPARE(evaluate().size(), 1);
    }

    void groupAny()
    {
        const auto a = addDevice(QStringLiteral("A"));
        const auto b = addDevice(QStringLiteral("B"));
        DeviceGroup g { QUuid::createUuid(), QStringLiteral("Rack"), { a, b } };
        config.groups << g;
        addRule(
            makeRule(g.id, ConditionType::ChargeBelow, 20, ActionType::Shutdown, 60, RuleScope::GroupAny));
        push(a, online());
        push(b, online());
        QVERIFY(fire().isEmpty());
        push(a, onBattery(50, 900));
        push(b, onBattery(18, 300));
        auto t = fire();
        QCOMPARE(t.size(), 1);
        QCOMPARE(t.first().deviceId, b);
        QCOMPARE(t.first().targetName, QStringLiteral("Rack"));
        QVERIFY(t.first().episodeKey.startsWith(u"g:"));

        // B recovers, A is still out: the group outage continues, no re-fire.
        for (int i = 0; i < 8; ++i) {
            clock.advance(5);
            push(a, onBattery(15, 200));
            push(b, online(20));
            det.tick(dev(a), clock.now);
            det.tick(dev(b), clock.now);
        }
        QVERIFY(!det.state(b)->inEpisode);
        QVERIFY(fire().isEmpty());
        QVERIFY(rules.isEpisodeActive(t.first().episodeKey, det.states()));

        // Everyone back on stable line power: the group outage is over.
        for (int i = 0; i < 8; ++i) {
            clock.advance(5);
            push(a, online(16));
            push(b, online(21));
        }
        QVERIFY(fire().isEmpty());
        QVERIFY(!rules.isEpisodeActive(t.first().episodeKey, det.states()));
        push(a, onBattery(15, 200));
        QCOMPARE(fire().size(), 1);
    }

    void groupAll()
    {
        const auto a = addDevice(QStringLiteral("A"));
        const auto b = addDevice(QStringLiteral("B"));
        DeviceGroup g { QUuid::createUuid(), QStringLiteral("Rack"), { a, b } };
        config.groups << g;
        addRule(
            makeRule(g.id, ConditionType::RuntimeBelow, 300, ActionType::Shutdown, 60, RuleScope::GroupAll));
        push(a, onBattery(20, 250));
        push(b, onBattery(60, 900));
        QVERIFY(fire().isEmpty());
        clock.advance(5);
        push(b, onBattery(30, 280));
        const auto t = fire();
        QCOMPARE(t.size(), 1);
        QCOMPARE(t.first().runtime, std::optional<qint64>(250));  // the lowest
        QVERIFY(t.first().reason.contains(QStringLiteral("A")));
        QVERIFY(t.first().reason.contains(QStringLiteral("B")));
        QVERIFY(fire().isEmpty());
    }

    void defaultRules()
    {
        auto d = makeDevice(QStringLiteral("A"), SourceType::Nut);
        auto r = defaultRulesForDevice(d);
        QCOMPARE(r.size(), 2);
        QCOMPARE(r.at(0).condition, ConditionType::LowBattery);
        QCOMPARE(r.at(1).condition, ConditionType::ForcedShutdown);
        QVERIFY(r.at(0).target == d.id && r.at(1).target == d.id);
        d.type = SourceType::EcoFlow;
        QCOMPARE(defaultRulesForDevice(d).size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestRuleEngine)
#include "tst_ruleengine.moc"
