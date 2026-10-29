// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "common/FakeHttpServer.h"
#include "common/FakeSource.h"
#include "core/engine/ActionRunner.h"
#include "core/engine/Monitor.h"
#include "core/model/Notifier.h"
#include "core/notify/Dispatcher.h"
#include "core/notify/Notifiers.h"
#include "core/storage/HistoryStore.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace nutty;
using namespace nutty::test;

namespace {

// Stands in for the desktop channel and keeps what it was given.
class DeskNotifier : public Notifier
{
public:
    static inline QList<Event> received;

    using Notifier::Notifier;

    void send(const Event& event) override
    {
        received << event;
        emit sent(channelId());
    }
};

QStringList eventTitles(const QList<Event>& events)
{
    QStringList out;
    for (const auto& e : events)
        out << e.title;
    return out;
}

}  // namespace

// From a UPS changing state to the notification arriving: two fake UPSes feed
// a Monitor, its events go through the Dispatcher like in the app, and the
// channels are a desktop stand-in plus real ntfy and webhook notifiers
// talking to local HTTP servers.
class TestAlerts : public QObject
{
    Q_OBJECT

private:
    FakeClock clock;
    std::unique_ptr<HistoryStore> history;
    std::unique_ptr<Monitor> monitor;
    std::unique_ptr<Dispatcher> dispatcher;
    std::unique_ptr<ActionRunner> runner;
    std::unique_ptr<FakeHttpServer> ntfy;
    std::unique_ptr<FakeHttpServer> hook;
    int ntfyStatus = 200;
    QString shutdownError;  // non-empty: the OS refuses to shut down
    QStringList ran;  // what the power backend was asked to do
    AppConfig config;
    QUuid upsA;  // goes to every channel
    QUuid upsB;  // goes to the desk only

    ChannelConfig& channel(const QString& name)
    {
        return *std::ranges::find(config.channels, name, &ChannelConfig::name);
    }
    QList<Event>& desk() { return DeskNotifier::received; }
    const QList<FakeHttpServer::Request>& phone() const { return ntfy->requests(); }
    const QList<FakeHttpServer::Request>& webhook() const { return hook->requests(); }

    void push(const QUuid& id, DeviceStatus s)
    {
        s.sampledAt = clock.now;
        FakeSource::find(id)->push(s);
    }
    // Both UPSes report every 5 s for `seconds`, ticking every second.
    void hold(const DeviceStatus& a, const DeviceStatus& b, int seconds)
    {
        for (int t = 1; t <= seconds; ++t) {
            clock.advance(1);
            if (t % 5 == 0) {
                push(upsA, a);
                push(upsB, b);
            }
            monitor->tick();
        }
    }
    void hold(const DeviceStatus& a, int seconds) { hold(a, online(), seconds); }
    void start()
    {
        monitor->applyConfig(config);
        dispatcher->applyConfig(config);
        push(upsA, online());
        push(upsB, online());
        hold(online(), 10);
    }
    // Waits until the HTTP channels have had `phoneCount` and `hookCount`
    // requests, then a little longer to catch any extra ones.
    bool delivered(qsizetype phoneCount, qsizetype hookCount)
    {
        if (!QTest::qWaitFor(
                [&] { return phone().size() >= phoneCount && webhook().size() >= hookCount; }, 5000))
            return false;
        QTest::qWait(50);
        return phone().size() == phoneCount && webhook().size() == hookCount;
    }
    static QString title(const FakeHttpServer::Request& r)
    {
        return QString::fromUtf8(r.headers.value("title"));
    }
    static QStringList titles(const QList<FakeHttpServer::Request>& requests)
    {
        QStringList out;
        for (const auto& r : requests)
            out << title(r);
        out.sort();
        return out;
    }
    static QJsonObject json(const FakeHttpServer::Request& r)
    {
        return QJsonDocument::fromJson(r.body).object();
    }

private slots:
    void initTestCase()
    {
        FakeSource::registerFor(SourceType::Nut);
        registerCoreNotifiers();
        NotifierRegistry::registerFactory(ChannelType::Desktop, [](const ChannelConfig& c, QObject* parent) {
            return std::make_unique<DeskNotifier>(c, parent);
        });
    }

    void init()
    {
        clock = {};
        desk().clear();
        ntfyStatus = 200;
        shutdownError.clear();
        ran.clear();

        ntfy = std::make_unique<FakeHttpServer>();
        ntfy->setHandler([this](const FakeHttpServer::Request&) {
            return FakeHttpServer::Response { ntfyStatus, "{}", "application/json" };
        });
        hook = std::make_unique<FakeHttpServer>();
        hook->setHandler([](const FakeHttpServer::Request&) {
            return FakeHttpServer::Response { 204, {}, "text/plain" };
        });

        history = std::make_unique<HistoryStore>();
        QVERIFY(history->open(QStringLiteral(":memory:")));
        monitor = std::make_unique<Monitor>(history.get());
        monitor->setAutoTick(false);
        monitor->setClock([this] { return clock.now; });
        dispatcher = std::make_unique<Dispatcher>();
        connect(monitor.get(), &Monitor::eventRaised, dispatcher.get(), &Dispatcher::dispatch);
        ActionRunner::Backend backend;
        backend.execute = [this](ActionType type) -> ActionRunner::Result {
            ran << enumKey(type);
            if (!shutdownError.isEmpty())
                return std::unexpected(shutdownError);
            return {};
        };
        backend.runCommand = [this](const QString& command) -> ActionRunner::Result {
            ran << command;
            return {};
        };
        runner = std::make_unique<ActionRunner>(monitor.get(), history.get(), std::move(backend));

        config = {};
        config.devices << makeDevice(QStringLiteral("UPS-A")) << makeDevice(QStringLiteral("UPS-B"));
        upsA = config.devices[0].id;
        upsB = config.devices[1].id;
        for (auto& d : config.devices) {
            d.notifyChargeBelow = 0;
            d.notifyRuntimeBelowSec = 0;
        }

        ChannelConfig phone;
        phone.id = QUuid::createUuid();
        phone.name = QStringLiteral("phone");
        phone.type = ChannelType::Ntfy;
        phone.events = { EventType::OnBattery, EventType::LowBattery, EventType::PowerRestored,
            EventType::ActionScheduled, EventType::ActionCancelled, EventType::ActionExecuted,
            EventType::ActionFailed };
        phone.settings = { { QString::fromLatin1(keys::NtfyServer), ntfy->baseUrl() },
            { QString::fromLatin1(keys::NtfyTopic), QStringLiteral("ups") },
            { QStringLiteral("retryDelayMs"), 10 } };

        ChannelConfig deskChannel;
        deskChannel.id = QUuid::createUuid();
        deskChannel.name = QStringLiteral("desk");
        deskChannel.type = ChannelType::Desktop;
        deskChannel.events = defaultChannelEvents(ChannelType::Desktop);

        ChannelConfig webhookChannel;
        webhookChannel.id = QUuid::createUuid();
        webhookChannel.name = QStringLiteral("hook");
        webhookChannel.type = ChannelType::Webhook;
        webhookChannel.events = { EventType::PowerRestored, EventType::ActionFailed };
        webhookChannel.settings
            = { { QString::fromLatin1(keys::WebhookUrl), hook->baseUrl() + QStringLiteral("/nutty") },
                  { QStringLiteral("retryDelayMs"), 10 } };

        config.channels = { phone, deskChannel, webhookChannel };
        config.devices[1].channels = { deskChannel.id };
    }

    void cleanup()
    {
        runner.reset();
        dispatcher.reset();
        monitor.reset();
        history.reset();
        ntfy.reset();
        hook.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    // On battery, low battery, the countdown, the shutdown: each channel gets
    // the events it asked for, with the device and the urgency right.
    void outageEndingInShutdown()
    {
        config.rules << makeRule(upsA, ConditionType::LowBattery, 0, ActionType::Shutdown, 60);
        start();
        QVERIFY(desk().isEmpty());
        QVERIFY(phone().isEmpty());

        push(upsA, onBattery(80, 1800));
        QCOMPARE(eventTitles(desk()), QStringList { QStringLiteral("On battery") });
        QCOMPARE(desk().first().deviceName, QStringLiteral("UPS-A"));
        QVERIFY(delivered(1, 0));
        const auto& onBatteryRequest = phone().first();
        QCOMPARE(onBatteryRequest.method, QByteArray("POST"));
        QCOMPARE(onBatteryRequest.path(), QByteArray("/ups"));
        QCOMPARE(title(onBatteryRequest), QStringLiteral("On battery"));
        QCOMPARE(onBatteryRequest.headers.value("priority"), QByteArray("4"));
        QCOMPARE(onBatteryRequest.headers.value("tags"), QByteArray("battery,warning"));
        QVERIFY(onBatteryRequest.body.contains("UPS-A is on battery"));
        QVERIFY(onBatteryRequest.body.contains("80"));

        hold(onBattery(60, 1200), 30);
        QCOMPARE(desk().size(), 1);

        push(upsA, onBattery(30, 600, {}, StatusFlag::LowBattery));
        QCOMPARE(eventTitles(desk()),
            (QStringList { QStringLiteral("On battery"), QStringLiteral("Low battery"),
                QStringLiteral("Shut down in 1 min") }));
        QVERIFY(delivered(3, 0));
        for (const auto& r : phone().mid(1))
            QCOMPARE(r.headers.value("priority"), QByteArray("5"));
        QCOMPARE(titles(phone().mid(1)),
            (QStringList { QStringLiteral("Low battery"), QStringLiteral("Shut down in 1 min") }));

        hold(onBattery(28, 560, {}, StatusFlag::LowBattery), 60);
        QCOMPARE(ran, QStringList { QStringLiteral("Shutdown") });
        QCOMPARE(desk().last().title, QStringLiteral("Shutdown started"));
        QCOMPARE(desk().last().type, EventType::ActionExecuted);
        QVERIFY(delivered(4, 0));
        QCOMPARE(titles(phone()).count(QStringLiteral("Shutdown started")), 1);
    }

    // Power comes back during the countdown: the shutdown is called off and
    // everyone who wants to know hears about it, the webhook included.
    void powerBackDuringTheCountdown()
    {
        config.rules << makeRule(upsA, ConditionType::LowBattery, 0, ActionType::Shutdown, 120);
        start();
        push(upsA, onBattery(30, 600, {}, StatusFlag::LowBattery));
        QVERIFY(monitor->pendingAction());
        hold(onBattery(29, 590, {}, StatusFlag::LowBattery), 20);
        desk().clear();
        QVERIFY(delivered(3, 0));
        ntfy->clearRequests();

        push(upsA, online(30));
        hold(online(31), 25);
        QVERIFY(desk().isEmpty());  // not until line power has held for 30 s
        hold(online(32), 5);
        QCOMPARE(eventTitles(desk()),
            (QStringList { QStringLiteral("Power restored"), QStringLiteral("Shutdown cancelled") }));
        QCOMPARE(desk().last().message, QStringLiteral("UPS-A: power was restored"));
        QVERIFY(delivered(2, 1));
        QCOMPARE(titles(phone()),
            (QStringList { QStringLiteral("Power restored"), QStringLiteral("Shutdown cancelled") }));
        for (const auto& r : phone())
            QCOMPARE(r.headers.value("priority"), QByteArray("3"));

        const auto& posted = webhook().first();
        QCOMPARE(posted.method, QByteArray("POST"));
        QCOMPARE(posted.path(), QByteArray("/nutty"));
        QCOMPARE(posted.headers.value("content-type"), QByteArray("application/json"));
        const auto body = json(posted);
        QCOMPARE(body.value(QStringLiteral("event")).toString(), QStringLiteral("PowerRestored"));
        QCOMPARE(body.value(QStringLiteral("severity")).toString(), QStringLiteral("info"));
        QCOMPARE(body.value(QStringLiteral("device")).toString(), QStringLiteral("UPS-A"));
        QCOMPARE(body.value(QStringLiteral("deviceId")).toString(), upsA.toString(QUuid::WithoutBraces));
        QVERIFY(body.value(QStringLiteral("message")).toString().startsWith(QStringLiteral("UPS-A is back")));

        hold(online(40), 180);
        QVERIFY(ran.isEmpty());
    }

    // UPS-B is routed to the desk alone.
    void perDeviceRouting()
    {
        start();
        push(upsB, onBattery(70, 1500));
        QCOMPARE(desk().size(), 1);
        QCOMPARE(desk().first().deviceName, QStringLiteral("UPS-B"));
        QCOMPARE(desk().first().deviceId, upsB);
        QVERIFY(delivered(0, 0));

        // Both on battery: the phone hears about UPS-A only.
        push(upsA, onBattery(70, 1500));
        QCOMPARE(desk().size(), 2);
        QVERIFY(delivered(1, 0));
        QVERIFY(phone().first().body.contains("UPS-A"));
    }

    void disabledChannelStaysQuiet()
    {
        channel(QStringLiteral("phone")).enabled = false;
        start();
        push(upsA, onBattery(70, 1500));
        QCOMPARE(desk().size(), 1);
        QVERIFY(delivered(0, 0));
    }

    // The per-device thresholds notify once per outage, and again in the
    // next one.
    void thresholdsOncePerOutage()
    {
        config.devices[0].notifyChargeBelow = 50;
        config.devices[0].notifyRuntimeBelowSec = 300;
        start();
        push(upsA, onBattery(60, 1000));
        hold(onBattery(51, 301), 20);
        QCOMPARE(eventTitles(desk()), QStringList { QStringLiteral("On battery") });

        push(upsA, onBattery(50, 900));
        QCOMPARE(desk().last().title, QStringLiteral("Battery below 50 %"));
        QCOMPARE(desk().last().type, EventType::ChargeBelow);
        hold(onBattery(45, 800), 30);
        push(upsA, onBattery(40, 300));
        QCOMPARE(desk().last().title, QStringLiteral("Runtime below 5 min"));
        QCOMPARE(desk().last().message, QStringLiteral("UPS-A has 5 min of runtime left"));
        hold(onBattery(20, 100), 60);
        QCOMPARE(desk().size(), 3);
        // The phone isn't subscribed to either.
        QVERIFY(delivered(1, 0));

        push(upsA, online(25));
        hold(online(30), 40);
        desk().clear();
        push(upsA, onBattery(30, 200));
        QCOMPARE(eventTitles(desk()),
            (QStringList { QStringLiteral("On battery"), QStringLiteral("Battery below 50 %"),
                QStringLiteral("Runtime below 5 min") }));
    }

    // A Notify rule's own name is the notification.
    void notifyRule()
    {
        auto rule = makeRule(upsA, ConditionType::OnBatteryFor, 60, ActionType::Notify, 0);
        rule.name = QStringLiteral("Still on battery");
        config.rules << rule;
        start();
        push(upsA, onBattery(90, 3000));
        hold(onBattery(88, 2900), 55);
        QCOMPARE(desk().size(), 1);
        hold(onBattery(87, 2850), 5);
        QCOMPARE(desk().last().title, QStringLiteral("Still on battery"));
        QCOMPARE(desk().last().message, QStringLiteral("UPS-A: on battery for 1 min"));
        QVERIFY(delivered(2, 0));
        QCOMPARE(titles(phone()),
            (QStringList { QStringLiteral("On battery"), QStringLiteral("Still on battery") }));
        hold(onBattery(80, 2500), 300);
        QCOMPARE(desk().size(), 2);
    }

    // The OS refuses to shut down: an alert saying why goes out.
    void failedShutdownAlerts()
    {
        shutdownError = QStringLiteral("Not allowed by polkit");
        config.rules << makeRule(upsA, ConditionType::LowBattery, 0, ActionType::Shutdown, 0);
        start();
        push(upsA, onBattery(15, 200, {}, StatusFlag::LowBattery));
        QCOMPARE(ran, QStringList { QStringLiteral("Shutdown") });
        QCOMPARE(desk().last().type, EventType::ActionFailed);
        QCOMPARE(desk().last().title, QStringLiteral("Shutdown failed"));
        QCOMPARE(desk().last().message, QStringLiteral("UPS-A: Not allowed by polkit"));
        QVERIFY(delivered(4, 1));  // on battery, low battery, scheduled, failed
        const auto body = json(webhook().first());
        QCOMPARE(body.value(QStringLiteral("event")).toString(), QStringLiteral("ActionFailed"));
        QCOMPARE(body.value(QStringLiteral("severity")).toString(), QStringLiteral("critical"));
        QCOMPARE(
            body.value(QStringLiteral("message")).toString(), QStringLiteral("UPS-A: Not allowed by polkit"));
        QVERIFY(titles(phone()).contains(QStringLiteral("Shutdown failed")));
    }

    void commLostAndRestored()
    {
        start();
        for (int t = 0; t < 15; ++t) {
            clock.advance(1);
            push(upsB, online());
            monitor->tick();
        }
        QCOMPARE(eventTitles(desk()), QStringList { QStringLiteral("Connection lost") });
        QCOMPARE(desk().first().deviceName, QStringLiteral("UPS-A"));
        push(upsA, online());
        QCOMPARE(eventTitles(desk()),
            (QStringList { QStringLiteral("Connection lost"), QStringLiteral("Connection restored") }));
        QVERIFY(delivered(0, 0));
    }

    // Simulations are labelled as such and don't wake anyone up.
    void simulationIsLabelled()
    {
        auto rule = makeRule(upsA, ConditionType::LowBattery, 0, ActionType::Shutdown, 10);
        config.rules << rule;
        start();
        QVERIFY(monitor->simulateRule(rule.id));
        QCOMPARE(desk().last().title, QStringLiteral("Shut down in 10 s (dry run)"));
        QCOMPARE(desk().last().severity, Severity::Info);
        hold(online(), 10);
        QCOMPARE(desk().last().title, QStringLiteral("Shutdown simulated"));
        QVERIFY(delivered(2, 0));
        for (const auto& r : phone())
            QCOMPARE(r.headers.value("priority"), QByteArray("3"));
        QVERIFY(ran.isEmpty());
    }

    // PagerDuty: each kind of trouble opens its own alert, and line power
    // coming back resolves them all.
    void pagerDutyAlertsResolveWhenPowerReturns()
    {
        FakeHttpServer pagerDuty;
        pagerDuty.setHandler([](const FakeHttpServer::Request&) {
            return FakeHttpServer::Response { 202, R"({"status":"success"})", "application/json" };
        });
        ChannelConfig pd;
        pd.id = QUuid::createUuid();
        pd.name = QStringLiteral("on-call");
        pd.type = ChannelType::PagerDuty;
        pd.events = defaultChannelEvents(ChannelType::PagerDuty);
        pd.settings = { { QStringLiteral("endpoint"), pagerDuty.baseUrl() + QStringLiteral("/v2/enqueue") },
            { QStringLiteral("retryDelayMs"), 10 } };
        pd.secrets = { { QString::fromLatin1(keys::PagerDutyRoutingKey), QStringLiteral("R0UT1NGK3Y") } };
        config.channels << pd;
        start();

        const auto sent = [&] {
            QList<QPair<QString, QString>> out;  // action, alert
            for (const auto& r : pagerDuty.requests()) {
                const auto body = json(r);
                out.append({ body.value(QStringLiteral("event_action")).toString(),
                    body.value(QStringLiteral("dedup_key")).toString().section(u':', 2) });
            }
            return out;
        };
        const auto trigger = QStringLiteral("trigger");
        const auto resolve = QStringLiteral("resolve");

        push(upsA, onBattery(80, 1800));
        QTRY_COMPARE(pagerDuty.requests().size(), 1);
        hold(onBattery(60, 1200), 30);
        push(upsA, onBattery(30, 600, {}, StatusFlag::LowBattery));
        QTRY_COMPARE(pagerDuty.requests().size(), 2);
        QCOMPARE(sent(),
            (QList<QPair<QString, QString>> {
                { trigger, QStringLiteral("OnBattery") }, { trigger, QStringLiteral("LowBattery") } }));
        const auto low = json(pagerDuty.requests().at(1)).value(QStringLiteral("payload")).toObject();
        QCOMPARE(low.value(QStringLiteral("severity")).toString(), QStringLiteral("critical"));
        QCOMPARE(low.value(QStringLiteral("source")).toString(), QStringLiteral("UPS-A"));

        // UPS-B isn't routed to it.
        push(upsB, onBattery(70, 1500));
        push(upsA, online(30));
        hold(online(31), online(31), 30);
        QTRY_COMPARE(pagerDuty.requests().size(), 7);
        const auto all = sent();
        for (const auto& [action, alert] : all.mid(2))
            QCOMPARE(action, resolve);
        QCOMPARE(all.mid(2).size(), 5);
        QVERIFY(all.contains(qMakePair(resolve, QStringLiteral("OnBattery"))));
        QVERIFY(all.contains(qMakePair(resolve, QStringLiteral("LowBattery"))));
        for (const auto& r : pagerDuty.requests())
            QVERIFY(json(r)
                    .value(QStringLiteral("dedup_key"))
                    .toString()
                    .contains(upsA.toString(QUuid::WithoutBraces)));
        QTest::qWait(50);
        QCOMPARE(pagerDuty.requests().size(), 7);
    }

    // One channel failing doesn't hold the others up.
    void failingChannelDoesNotBlockTheOthers()
    {
        ntfyStatus = 500;
        start();
        QSignalSpy failed(dispatcher.get(), &Dispatcher::channelFailed);
        push(upsA, onBattery(70, 1500));
        QCOMPARE(desk().size(), 1);
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(failed.first().at(0).value<QUuid>(), channel(QStringLiteral("phone")).id);
        QVERIFY(phone().size() >= 2);  // it tried again first
    }
};

QTEST_GUILESS_MAIN(TestAlerts)
#include "tst_alerts.moc"
