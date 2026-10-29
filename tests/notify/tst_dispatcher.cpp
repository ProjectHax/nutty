// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/model/Notifier.h"
#include "core/notify/Dispatcher.h"
#include "core/notify/Notifiers.h"

#include <QPointer>
#include <QSignalSpy>
#include <QTest>

using namespace nutty;

namespace {

// Records what it was asked to send. The test drives success or failure
// through the static switches below.
class FakeNotifier : public Notifier
{
public:
    static inline int created = 0;
    static inline bool failNext = false;
    static inline QList<QPair<QUuid, Event>> deliveries;
    static inline QList<QPointer<FakeNotifier>> instances;

    explicit FakeNotifier(const ChannelConfig& config, QObject* parent)
        : Notifier(config, parent)
    {
        ++created;
        instances << this;
    }

    void send(const Event& event) override
    {
        deliveries.append({ channelId(), event });
        if (failNext)
            emit failed(channelId(), QStringLiteral("fake failure"));
        else
            emit sent(channelId());
    }

    void invoke(const QString& action) { emit actionInvoked(action); }
};

ChannelConfig makeChannel(
    const QString& name, QSet<EventType> events = { EventType::OnBattery, EventType::Test })
{
    ChannelConfig channel;
    channel.id = QUuid::createUuid();
    channel.name = name;
    channel.type = ChannelType::Desktop;  // the fake stands in for Desktop
    channel.events = std::move(events);
    return channel;
}

Event eventFor(const QUuid& deviceId, EventType type = EventType::OnBattery)
{
    Event event;
    event.type = type;
    event.deviceId = deviceId;
    event.title = QStringLiteral("t");
    return event;
}

}  // namespace

class TestDispatcher : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        NotifierRegistry::registerFactory(
            ChannelType::Desktop, [](const ChannelConfig& config, QObject* parent) {
                return std::make_unique<FakeNotifier>(config, parent);
            });
    }

    void init()
    {
        FakeNotifier::created = 0;
        FakeNotifier::failNext = false;
        FakeNotifier::deliveries.clear();
        FakeNotifier::instances.clear();
    }

    void coreNotifiersRegister()
    {
        registerCoreNotifiers();
        QVERIFY(NotifierRegistry::hasFactory(ChannelType::Pushover));
        QVERIFY(NotifierRegistry::hasFactory(ChannelType::Ntfy));
        QVERIFY(NotifierRegistry::hasFactory(ChannelType::Email));
        QVERIFY(NotifierRegistry::hasFactory(ChannelType::Webhook));
        QVERIFY(NotifierRegistry::hasFactory(ChannelType::PagerDuty));
    }

    void routing()
    {
        const auto all = makeChannel(QStringLiteral("all"));
        const auto picked = makeChannel(QStringLiteral("picked"));
        auto disabled = makeChannel(QStringLiteral("disabled"));
        disabled.enabled = false;

        DeviceConfig everyChannel;
        everyChannel.id = QUuid::createUuid();
        DeviceConfig onlyPicked;
        onlyPicked.id = QUuid::createUuid();
        onlyPicked.channels = { picked.id };

        AppConfig config;
        config.channels = { all, picked, disabled };
        config.devices = { everyChannel, onlyPicked };

        // A device without a channel list goes everywhere.
        QVERIFY(Dispatcher::routes(all, eventFor(everyChannel.id), config));
        QVERIFY(Dispatcher::routes(picked, eventFor(everyChannel.id), config));
        // A device with a list goes only there.
        QVERIFY(!Dispatcher::routes(all, eventFor(onlyPicked.id), config));
        QVERIFY(Dispatcher::routes(picked, eventFor(onlyPicked.id), config));
        // Events that aren't about a device, and unknown devices, go everywhere.
        QVERIFY(Dispatcher::routes(all, eventFor({}), config));
        QVERIFY(Dispatcher::routes(all, eventFor(QUuid::createUuid()), config));
        // Disabled channels and unsubscribed event types never route.
        QVERIFY(!Dispatcher::routes(disabled, eventFor(everyChannel.id), config));
        QVERIFY(!Dispatcher::routes(all, eventFor(everyChannel.id, EventType::PowerRestored), config));
    }

    void dispatchFollowsRoutes()
    {
        const auto all = makeChannel(QStringLiteral("all"));
        const auto picked = makeChannel(QStringLiteral("picked"));
        DeviceConfig device;
        device.id = QUuid::createUuid();
        device.channels = { picked.id };
        AppConfig config;
        config.channels = { all, picked };
        config.devices = { device };

        Dispatcher dispatcher;
        QSignalSpy sent(&dispatcher, &Dispatcher::channelSent);
        dispatcher.applyConfig(config);
        QCOMPARE(FakeNotifier::created, 2);
        QCOMPARE(dispatcher.activeChannels().size(), 2);

        dispatcher.dispatch(eventFor(device.id));
        QCOMPARE(FakeNotifier::deliveries.size(), 1);
        QCOMPARE(FakeNotifier::deliveries.first().first, picked.id);
        QCOMPARE(sent.size(), 1);
        QCOMPARE(sent.first().first().toUuid(), picked.id);

        dispatcher.dispatch(eventFor({}));
        QCOMPARE(FakeNotifier::deliveries.size(), 3);
    }

    void applyConfigKeepsRecreatesAndDrops()
    {
        auto keep = makeChannel(QStringLiteral("keep"));
        auto change = makeChannel(QStringLiteral("change"));
        auto drop = makeChannel(QStringLiteral("drop"));
        AppConfig config;
        config.channels = { keep, change, drop };

        Dispatcher dispatcher;
        dispatcher.applyConfig(config);
        QCOMPARE(FakeNotifier::created, 3);
        QPointer<FakeNotifier> keepNotifier;
        QPointer<FakeNotifier> changeNotifier;
        QPointer<FakeNotifier> dropNotifier;
        for (const auto& instance : std::as_const(FakeNotifier::instances)) {
            if (instance->channelId() == keep.id)
                keepNotifier = instance;
            else if (instance->channelId() == change.id)
                changeNotifier = instance;
            else
                dropNotifier = instance;
        }

        // Routing-only edits keep the notifier; delivery edits replace it.
        keep.events.insert(EventType::PowerRestored);
        change.settings.insert(QStringLiteral("server"), QStringLiteral("elsewhere"));
        config.channels = { keep, change };
        dispatcher.applyConfig(config);
        QCOMPARE(FakeNotifier::created, 4);
        QTRY_VERIFY(!dropNotifier);
        QTRY_VERIFY(!changeNotifier);
        QVERIFY(keepNotifier);
        QCOMPARE(dispatcher.activeChannels().size(), 2);

        // The new routing applies to the kept notifier.
        dispatcher.dispatch(eventFor({}, EventType::PowerRestored));
        QCOMPARE(FakeNotifier::deliveries.size(), 1);
        QCOMPARE(FakeNotifier::deliveries.first().first, keep.id);

        // Disabling drops it too.
        keep.enabled = false;
        config.channels = { keep, change };
        dispatcher.applyConfig(config);
        QTRY_VERIFY(!keepNotifier);
        QCOMPARE(dispatcher.activeChannels(), QList<QUuid> { change.id });
    }

    void failuresAndActionsPassThrough()
    {
        const auto channel = makeChannel(QStringLiteral("c"));
        AppConfig config;
        config.channels = { channel };
        Dispatcher dispatcher;
        QSignalSpy failed(&dispatcher, &Dispatcher::channelFailed);
        QSignalSpy actions(&dispatcher, &Dispatcher::actionInvoked);
        dispatcher.applyConfig(config);

        FakeNotifier::failNext = true;
        dispatcher.dispatch(eventFor({}));
        QCOMPARE(failed.size(), 1);
        QCOMPARE(failed.first().at(0).toUuid(), channel.id);
        QCOMPARE(failed.first().at(1).toString(), QStringLiteral("fake failure"));

        FakeNotifier::instances.first()->invoke(QStringLiteral("cancel"));
        QCOMPARE(actions.size(), 1);
        QCOMPARE(actions.first().first().toString(), QStringLiteral("cancel"));
    }

    void sendTest()
    {
        auto channel = makeChannel(QStringLiteral("unsaved"));
        channel.events.clear();  // tests ignore subscriptions
        Dispatcher dispatcher;
        QSignalSpy finished(&dispatcher, &Dispatcher::testFinished);

        dispatcher.sendTest(channel);
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.first().at(0).toUuid(), channel.id);
        QCOMPARE(finished.first().at(1).toBool(), true);
        QCOMPARE(FakeNotifier::deliveries.first().second.type, EventType::Test);
        // The temporary notifier goes away and isn't an active channel.
        QVERIFY(dispatcher.activeChannels().isEmpty());
        QTRY_VERIFY(!FakeNotifier::instances.first());

        FakeNotifier::failNext = true;
        dispatcher.sendTest(channel);
        QCOMPARE(finished.size(), 2);
        QCOMPARE(finished.at(1).at(1).toBool(), false);
        QCOMPARE(finished.at(1).at(2).toString(), QStringLiteral("fake failure"));
    }

    void testEvent()
    {
        const auto event = Dispatcher::testEvent();
        QCOMPARE(event.type, EventType::Test);
        QVERIFY(!event.title.isEmpty());
        QVERIFY(event.timestamp.isValid());
    }
};

QTEST_GUILESS_MAIN(TestDispatcher)
#include "tst_dispatcher.moc"
