// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "common/FakeHttpServer.h"
#include "core/model/DeviceStatus.h"
#include "core/notify/NotifyUtil.h"
#include "core/notify/NtfyNotifier.h"
#include "core/notify/PagerDutyNotifier.h"
#include "core/notify/PushoverNotifier.h"
#include "core/notify/WebhookNotifier.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>
#include <QTimeZone>
#include <QUrlQuery>

using namespace nutty;

namespace {

// Every request here goes to an in-process server on 127.0.0.1.

Event sampleEvent()
{
    Event event;
    event.type = EventType::OnBattery;
    event.severity = Severity::Warning;
    event.timestamp = QDateTime(QDate(2026, 3, 4), QTime(5, 6, 7), QTimeZone::utc());
    event.deviceId = QUuid::createUuid();
    event.deviceName = QStringLiteral("Rack UPS");
    event.title = QStringLiteral("On battery");
    event.message = QStringLiteral("Rack UPS is on battery: 87 %, 24 min left");
    event.flags = StatusFlags(StatusFlag::OnBattery) | StatusFlag::Discharging;
    event.charge = 87.4;
    event.runtime = 1440;
    event.load = 31.0;
    return event;
}

ChannelConfig channel(ChannelType type, QVariantMap settings, QVariantMap secrets = {})
{
    ChannelConfig config;
    config.id = QUuid::createUuid();
    config.type = type;
    config.name = QStringLiteral("test");
    settings.insert(QStringLiteral("retryDelayMs"), 10);
    config.settings = std::move(settings);
    config.secrets = std::move(secrets);
    return config;
}

QString key(const char* name)
{
    return QString::fromLatin1(name);
}

ChannelConfig pagerDuty(const FakeHttpServer& server)
{
    return channel(ChannelType::PagerDuty,
        { { QStringLiteral("endpoint"), server.baseUrl() + QStringLiteral("/v2/enqueue") } },
        { { key(keys::PagerDutyRoutingKey), QStringLiteral("R0UT1NGK3Y") } });
}

FakeHttpServer::Response json(int status, const QByteArray& body)
{
    return { status, body, "application/json" };
}

// Sends one event; true when sent() came, false on failed() (error filled in).
bool deliver(Notifier& notifier, const Event& event, QString* error = nullptr)
{
    QSignalSpy sent(&notifier, &Notifier::sent);
    QSignalSpy failed(&notifier, &Notifier::failed);
    notifier.send(event);
    if (!QTest::qWaitFor([&] { return !sent.isEmpty() || !failed.isEmpty(); }, 10000))
        return false;
    if (!failed.isEmpty() && error)
        *error = failed.at(0).at(1).toString();
    return !sent.isEmpty();
}

}  // namespace

class TestHttpNotifiers : public QObject
{
    Q_OBJECT

private slots:
    void pushoverForm()
    {
        FakeHttpServer server;
        server.setHandler(
            [](const FakeHttpServer::Request&) { return json(200, R"({"status":1,"request":"x"})"); });
        PushoverNotifier notifier(channel(ChannelType::Pushover,
            { { QStringLiteral("endpoint"), server.baseUrl() + QStringLiteral("/1/messages.json") },
                { key(keys::PushoverDevice), QStringLiteral("phone") },
                { key(keys::PushoverSound), QStringLiteral("siren") } },
            { { key(keys::PushoverApiToken), QStringLiteral("tok&en") },
                { key(keys::PushoverUserKey), QStringLiteral("user key") } }));

        auto event = sampleEvent();
        event.severity = Severity::Critical;
        QString error;
        QVERIFY2(deliver(notifier, event, &error), qPrintable(error));

        QCOMPARE(server.requests().size(), 1);
        const auto& request = server.requests().first();
        QCOMPARE(request.method, QByteArray("POST"));
        QCOMPARE(request.path(), QByteArray("/1/messages.json"));
        QCOMPARE(request.headers.value("content-type"), QByteArray("application/x-www-form-urlencoded"));
        QVERIFY(request.headers.value("user-agent").startsWith("nutty/"));

        const QUrlQuery form(QString::fromLatin1(request.body));
        QCOMPARE(form.queryItemValue(QStringLiteral("token"), QUrl::FullyDecoded), QStringLiteral("tok&en"));
        QCOMPARE(form.queryItemValue(QStringLiteral("user"), QUrl::FullyDecoded), QStringLiteral("user key"));
        QCOMPARE(form.queryItemValue(QStringLiteral("title"), QUrl::FullyDecoded), event.title);
        QCOMPARE(form.queryItemValue(QStringLiteral("message"), QUrl::FullyDecoded), event.message);
        QCOMPARE(form.queryItemValue(QStringLiteral("priority")), QStringLiteral("1"));
        QCOMPARE(form.queryItemValue(QStringLiteral("timestamp")),
            QString::number(event.timestamp.toSecsSinceEpoch()));
        QCOMPARE(form.queryItemValue(QStringLiteral("device")), QStringLiteral("phone"));
        QCOMPARE(form.queryItemValue(QStringLiteral("sound")), QStringLiteral("siren"));
    }

    void pushoverPriorities()
    {
        QCOMPARE(PushoverNotifier::priorityFor(Severity::Info), -1);
        QCOMPARE(PushoverNotifier::priorityFor(Severity::Warning), 0);
        QCOMPARE(PushoverNotifier::priorityFor(Severity::Critical), 1);
    }

    void pushoverApiError()
    {
        FakeHttpServer server;
        server.setHandler([](const FakeHttpServer::Request&) {
            return json(400, R"({"status":0,"errors":["application token is invalid"]})");
        });
        PushoverNotifier notifier(channel(ChannelType::Pushover,
            { { QStringLiteral("endpoint"), server.baseUrl() + QStringLiteral("/1/messages.json") } },
            { { key(keys::PushoverApiToken), QStringLiteral("t") },
                { key(keys::PushoverUserKey), QStringLiteral("u") } }));
        QString error;
        QVERIFY(!deliver(notifier, sampleEvent(), &error));
        QVERIFY2(error.contains(QStringLiteral("application token is invalid")), qPrintable(error));
        // 4xx isn't retried.
        QCOMPARE(server.requests().size(), 1);
    }

    void pushoverStatusZeroIsFailure()
    {
        FakeHttpServer server;
        server.setHandler([](const FakeHttpServer::Request&) { return json(200, R"({"status":0})"); });
        PushoverNotifier notifier(channel(ChannelType::Pushover,
            { { QStringLiteral("endpoint"), server.baseUrl() + QStringLiteral("/1/messages.json") } },
            { { key(keys::PushoverApiToken), QStringLiteral("t") },
                { key(keys::PushoverUserKey), QStringLiteral("u") } }));
        QVERIFY(!deliver(notifier, sampleEvent()));
    }

    void pushoverNeedsSecrets()
    {
        FakeHttpServer server;
        PushoverNotifier notifier(channel(ChannelType::Pushover,
            { { QStringLiteral("endpoint"), server.baseUrl() + QStringLiteral("/1/messages.json") } }));
        QString error;
        QVERIFY(!deliver(notifier, sampleEvent(), &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(server.requests().size(), 0);
    }

    void ntfyHeaders()
    {
        FakeHttpServer server;
        server.setHandler([](const FakeHttpServer::Request&) { return json(200, "{}"); });
        NtfyNotifier notifier(channel(ChannelType::Ntfy,
            { { key(keys::NtfyServer), server.baseUrl() + u'/' },
                { key(keys::NtfyTopic), QStringLiteral("ups alerts") } },
            { { key(keys::NtfyToken), QStringLiteral("tk_123") } }));

        auto event = sampleEvent();
        event.severity = Severity::Critical;
        event.title = QStringLiteral("Batterie schwach");
        QString error;
        QVERIFY2(deliver(notifier, event, &error), qPrintable(error));

        const auto& request = server.requests().first();
        QCOMPARE(request.method, QByteArray("POST"));
        QCOMPARE(request.path(), QByteArray("/ups%20alerts"));
        QCOMPARE(request.headers.value("title"), QByteArray("Batterie schwach"));
        QCOMPARE(request.headers.value("priority"), QByteArray("5"));
        QCOMPARE(request.headers.value("tags"), NtfyNotifier::tagsFor(event));
        QCOMPARE(request.headers.value("authorization"), QByteArray("Bearer tk_123"));
        QCOMPARE(request.body, event.message.toUtf8());
    }

    void ntfyEncodesTitleAndUsesBasicAuth()
    {
        FakeHttpServer server;
        server.setHandler([](const FakeHttpServer::Request&) { return json(200, "{}"); });
        NtfyNotifier notifier(channel(ChannelType::Ntfy,
            { { key(keys::NtfyServer), server.baseUrl() }, { key(keys::NtfyTopic), QStringLiteral("t") },
                { key(keys::NtfyUsername), QStringLiteral("me") } },
            { { key(keys::NtfyPassword), QStringLiteral("p:w") } }));

        auto event = sampleEvent();
        event.severity = Severity::Info;
        event.title = QStringLiteral("Strom zurück");
        QVERIFY(deliver(notifier, event));

        const auto& request = server.requests().first();
        QCOMPARE(request.headers.value("priority"), QByteArray("3"));
        QCOMPARE(request.headers.value("authorization"), "Basic " + QByteArray("me:p:w").toBase64());
        const auto title = request.headers.value("title");
        QVERIFY2(title.startsWith("=?UTF-8?B?"), title.constData());
        QCOMPARE(QString::fromUtf8(QByteArray::fromBase64(title.mid(10, title.size() - 12))), event.title);
    }

    void ntfyRetriesServerErrors()
    {
        FakeHttpServer server;
        int calls = 0;
        server.setHandler(
            [&calls](const FakeHttpServer::Request&) { return json(++calls == 1 ? 503 : 200, "{}"); });
        NtfyNotifier notifier(channel(ChannelType::Ntfy,
            { { key(keys::NtfyServer), server.baseUrl() }, { key(keys::NtfyTopic), QStringLiteral("t") } }));
        QVERIFY(deliver(notifier, sampleEvent()));
        QCOMPARE(server.requests().size(), 2);
    }

    void ntfyNeedsTopic()
    {
        NtfyNotifier notifier(
            channel(ChannelType::Ntfy, { { key(keys::NtfyServer), QStringLiteral("http://127.0.0.1:9") } }));
        QVERIFY(!deliver(notifier, sampleEvent()));
    }

    void webhookJsonPreset()
    {
        FakeHttpServer server;
        server.setHandler([](const FakeHttpServer::Request&) { return json(204, {}); });
        WebhookNotifier notifier(channel(
            ChannelType::Webhook, { { key(keys::WebhookUrl), server.baseUrl() + QStringLiteral("/hook") } }));
        const auto event = sampleEvent();
        QString error;
        QVERIFY2(deliver(notifier, event, &error), qPrintable(error));

        const auto& request = server.requests().first();
        QCOMPARE(request.method, QByteArray("POST"));
        QCOMPARE(request.path(), QByteArray("/hook"));
        QCOMPARE(request.headers.value("content-type"), QByteArray("application/json"));
        const auto body = QJsonDocument::fromJson(request.body).object();
        QCOMPARE(body.value(QStringLiteral("event")).toString(), enumKey(EventType::OnBattery));
        QCOMPARE(body.value(QStringLiteral("severity")).toString(), QStringLiteral("warning"));
        QCOMPARE(body.value(QStringLiteral("device")).toString(), event.deviceName);
        QCOMPARE(
            body.value(QStringLiteral("deviceId")).toString(), event.deviceId.toString(QUuid::WithoutBraces));
        QCOMPARE(body.value(QStringLiteral("title")).toString(), event.title);
        QCOMPARE(body.value(QStringLiteral("message")).toString(), event.message);
        QCOMPARE(body.value(QStringLiteral("charge")).toDouble(), 87.4);
        QCOMPARE(body.value(QStringLiteral("runtime")).toDouble(), 1440.0);
        QCOMPARE(body.value(QStringLiteral("status")).toString(), statusTokens(event.flags));
        QCOMPARE(body.value(QStringLiteral("timestamp")).toString(), QStringLiteral("2026-03-04T05:06:07Z"));
    }

    void webhookCustomJsonTemplateIsEscaped()
    {
        auto event = sampleEvent();
        event.message = QStringLiteral("He said \"run\"\\\nnow\ttab");
        const auto body = WebhookNotifier::buildBody(QStringLiteral("custom"),
            QStringLiteral(
                R"({"text":"{title}: {message}","charge":{charge},"left":"{runtime_min} min","keep":"{nope}"})"),
            event);
        QCOMPARE(body.contentType, QByteArray("application/json"));
        QJsonParseError parseError;
        const auto object = QJsonDocument::fromJson(body.data, &parseError).object();
        QVERIFY2(parseError.error == QJsonParseError::NoError, body.data.constData());
        QCOMPARE(object.value(QStringLiteral("text")).toString(),
            event.title + QStringLiteral(": ") + event.message);
        QCOMPARE(object.value(QStringLiteral("charge")).toInt(), 87);
        QCOMPARE(object.value(QStringLiteral("left")).toString(), QStringLiteral("24 min"));
        QCOMPARE(object.value(QStringLiteral("keep")).toString(), QStringLiteral("{nope}"));
    }

    void webhookCustomPlainTemplate()
    {
        const auto event = sampleEvent();
        const auto body = WebhookNotifier::buildBody(
            QStringLiteral("custom"), QStringLiteral("{device} \"{status}\" at {charge}%"), event);
        QCOMPARE(body.contentType, QByteArray("text/plain; charset=utf-8"));
        QCOMPARE(QString::fromUtf8(body.data),
            QStringLiteral("Rack UPS \"%1\" at 87%").arg(statusTokens(event.flags)));
    }

    void webhookPresets()
    {
        const auto event = sampleEvent();
        const auto discord
            = QJsonDocument::fromJson(WebhookNotifier::buildBody(QStringLiteral("discord"), {}, event).data);
        QVERIFY(discord.object().value(QStringLiteral("content")).toString().contains(event.message));

        const auto slack
            = QJsonDocument::fromJson(WebhookNotifier::buildBody(QStringLiteral("slack"), {}, event).data);
        QVERIFY(slack.object().value(QStringLiteral("text")).toString().contains(event.title));

        const auto teams
            = QJsonDocument::fromJson(WebhookNotifier::buildBody(QStringLiteral("teams"), {}, event).data)
                  .object();
        QCOMPARE(teams.value(QStringLiteral("@type")).toString(), QStringLiteral("MessageCard"));
        QCOMPARE(teams.value(QStringLiteral("title")).toString(), event.title);
        QCOMPARE(teams.value(QStringLiteral("text")).toString(), event.message);
        QVERIFY(!teams.value(QStringLiteral("themeColor")).toString().isEmpty());
    }

    void webhookPutWithHeaders()
    {
        FakeHttpServer server;
        server.setHandler([](const FakeHttpServer::Request&) { return json(200, "ok"); });
        WebhookNotifier notifier(channel(ChannelType::Webhook,
            { { key(keys::WebhookUrl), server.baseUrl() + QStringLiteral("/put") },
                { key(keys::WebhookMethod), QStringLiteral("put") },
                { key(keys::WebhookHeaders), QStringLiteral("X-One: 1\n\nnot a header\nX-Two:  two words ") },
                { key(keys::WebhookAuthHeader), QStringLiteral("Authorization") } },
            { { key(keys::WebhookAuthValue), QStringLiteral("Bearer abc") } }));
        QVERIFY(deliver(notifier, sampleEvent()));

        const auto& request = server.requests().first();
        QCOMPARE(request.method, QByteArray("PUT"));
        QCOMPARE(request.headers.value("x-one"), QByteArray("1"));
        QCOMPARE(request.headers.value("x-two"), QByteArray("two words"));
        QCOMPARE(request.headers.value("authorization"), QByteArray("Bearer abc"));
    }

    void webhookRetriesOnce()
    {
        FakeHttpServer server;
        int calls = 0;
        server.setHandler(
            [&calls](const FakeHttpServer::Request&) { return json(++calls == 1 ? 404 : 200, "{}"); });
        WebhookNotifier notifier(
            channel(ChannelType::Webhook, { { key(keys::WebhookUrl), server.baseUrl() } }));
        QVERIFY(deliver(notifier, sampleEvent()));
        QCOMPARE(server.requests().size(), 2);

        server.clearRequests();
        server.setHandler([](const FakeHttpServer::Request&) { return json(500, "down"); });
        QString error;
        QVERIFY(!deliver(notifier, sampleEvent(), &error));
        QVERIFY2(error.contains(QStringLiteral("500")), qPrintable(error));
        QCOMPARE(server.requests().size(), 2);
    }

    void webhookRejectsBadUrl()
    {
        WebhookNotifier notifier(
            channel(ChannelType::Webhook, { { key(keys::WebhookUrl), QStringLiteral("ftp://x") } }));
        QVERIFY(!deliver(notifier, sampleEvent()));
    }

    void pagerDutyTrigger()
    {
        FakeHttpServer server;
        server.setHandler([](const FakeHttpServer::Request&) {
            return json(202, R"({"status":"success","message":"Event processed","dedup_key":"x"})");
        });
        const auto config = pagerDuty(server);
        PagerDutyNotifier notifier(config);
        const auto event = sampleEvent();
        QString error;
        QVERIFY2(deliver(notifier, event, &error), qPrintable(error));

        QCOMPARE(server.requests().size(), 1);
        const auto& request = server.requests().first();
        QCOMPARE(request.method, QByteArray("POST"));
        QCOMPARE(request.path(), QByteArray("/v2/enqueue"));
        QCOMPARE(request.headers.value("content-type"), QByteArray("application/json"));
        const auto body = QJsonDocument::fromJson(request.body).object();
        QCOMPARE(body.value(QStringLiteral("routing_key")).toString(), QStringLiteral("R0UT1NGK3Y"));
        QCOMPARE(body.value(QStringLiteral("event_action")).toString(), QStringLiteral("trigger"));
        QCOMPARE(body.value(QStringLiteral("dedup_key")).toString(),
            QStringLiteral("nutty:%1:OnBattery").arg(event.deviceId.toString(QUuid::WithoutBraces)));
        QCOMPARE(body.value(QStringLiteral("client")).toString(), QStringLiteral("nutty"));
        QVERIFY(body.value(QStringLiteral("client_url")).toString().startsWith(QStringLiteral("https://")));

        const auto payload = body.value(QStringLiteral("payload")).toObject();
        QCOMPARE(payload.value(QStringLiteral("summary")).toString(),
            QStringLiteral("On battery — Rack UPS is on battery: 87 %, 24 min left"));
        QCOMPARE(payload.value(QStringLiteral("source")).toString(), QStringLiteral("Rack UPS"));
        QCOMPARE(payload.value(QStringLiteral("severity")).toString(), QStringLiteral("warning"));
        QCOMPARE(payload.value(QStringLiteral("timestamp")).toString(),
            QStringLiteral("2026-03-04T05:06:07.000Z"));
        QCOMPARE(payload.value(QStringLiteral("class")).toString(), QStringLiteral("OnBattery"));
        const auto details = payload.value(QStringLiteral("custom_details")).toObject();
        QCOMPARE(details.value(QStringLiteral("message")).toString(), event.message);
        QCOMPARE(details.value(QStringLiteral("charge")).toDouble(), 87.4);
        QCOMPARE(details.value(QStringLiteral("runtime")).toDouble(), 1440.0);
        QCOMPARE(details.value(QStringLiteral("load")).toDouble(), 31.0);
    }

    void pagerDutySeverities()
    {
        QCOMPARE(PagerDutyNotifier::severityFor(Severity::Info), QStringLiteral("info"));
        QCOMPARE(PagerDutyNotifier::severityFor(Severity::Warning), QStringLiteral("warning"));
        QCOMPARE(PagerDutyNotifier::severityFor(Severity::Critical), QStringLiteral("critical"));
    }

    void pagerDutyRegions()
    {
        QCOMPARE(PagerDutyNotifier::endpointFor(QString()),
            QUrl(QStringLiteral("https://events.pagerduty.com/v2/enqueue")));
        QCOMPARE(PagerDutyNotifier::endpointFor(QStringLiteral("us")),
            QUrl(QStringLiteral("https://events.pagerduty.com/v2/enqueue")));
        QCOMPARE(PagerDutyNotifier::endpointFor(QStringLiteral("eu")),
            QUrl(QStringLiteral("https://events.eu.pagerduty.com/v2/enqueue")));
    }

    // Which alert each event opens, adds to or resolves.
    void pagerDutyAlerts_data()
    {
        QTest::addColumn<EventType>("type");
        QTest::addColumn<bool>("dryRun");
        QTest::addColumn<QString>("action");
        QTest::addColumn<QStringList>("alerts");
        const QStringList power { QStringLiteral("OnBattery"), QStringLiteral("LowBattery"),
            QStringLiteral("ChargeBelow"), QStringLiteral("RuntimeBelow"), QStringLiteral("ForcedShutdown") };
        const auto one = [](const char* alert) { return QStringList { QString::fromLatin1(alert) }; };
        const auto trigger = QStringLiteral("trigger");
        const auto resolve = QStringLiteral("resolve");
        QTest::newRow("on battery") << EventType::OnBattery << false << trigger << one("OnBattery");
        QTest::newRow("low battery") << EventType::LowBattery << false << trigger << one("LowBattery");
        QTest::newRow("charge below") << EventType::ChargeBelow << false << trigger << one("ChargeBelow");
        QTest::newRow("runtime below") << EventType::RuntimeBelow << false << trigger << one("RuntimeBelow");
        QTest::newRow("forced shutdown")
            << EventType::ForcedShutdown << false << trigger << one("ForcedShutdown");
        QTest::newRow("power restored") << EventType::PowerRestored << false << resolve << power;
        QTest::newRow("comm lost") << EventType::CommLost << false << trigger << one("CommLost");
        QTest::newRow("comm restored") << EventType::CommRestored << false << resolve << one("CommLost");
        QTest::newRow("scheduled") << EventType::ActionScheduled << false << trigger << one("action");
        QTest::newRow("executed") << EventType::ActionExecuted << false << trigger << one("action");
        QTest::newRow("simulated") << EventType::ActionExecuted << true << resolve << one("action");
        QTest::newRow("cancelled") << EventType::ActionCancelled << false << resolve << one("action");
        QTest::newRow("failed") << EventType::ActionFailed << false << trigger << one("ActionFailed");
        QTest::newRow("overload") << EventType::Overload << false << trigger << one("Overload");
        QTest::newRow("replace battery")
            << EventType::ReplaceBattery << false << trigger << one("ReplaceBattery");
        // A test opens a new alert every time.
        QTest::newRow("test") << EventType::Test << false << trigger << QStringList { QString() };
    }

    void pagerDutyAlerts()
    {
        QFETCH(EventType, type);
        QFETCH(bool, dryRun);
        QFETCH(QString, action);
        QFETCH(QStringList, alerts);
        FakeHttpServer server;
        server.setHandler([](const FakeHttpServer::Request&) { return json(202, "{}"); });
        PagerDutyNotifier notifier(pagerDuty(server));
        auto event = sampleEvent();
        event.type = type;
        if (type == EventType::Test)
            event.deviceId = {};
        event.extra.insert(QStringLiteral("dryRun"), dryRun);
        QVERIFY(deliver(notifier, event));

        QCOMPARE(server.requests().size(), alerts.size());
        for (qsizetype i = 0; i < alerts.size(); ++i) {
            const auto body = QJsonDocument::fromJson(server.requests().at(i).body).object();
            QCOMPARE(body.value(QStringLiteral("event_action")).toString(), action);
            const auto expected = alerts.at(i).isEmpty()
                ? QString()
                : PagerDutyNotifier::dedupKey(event.deviceId, alerts.at(i));
            QCOMPARE(body.value(QStringLiteral("dedup_key")).toString(), expected);
            QCOMPARE(body.contains(QStringLiteral("payload")), action == u"trigger");
            QCOMPARE(body.value(QStringLiteral("routing_key")).toString(), QStringLiteral("R0UT1NGK3Y"));
        }
    }

    void pagerDutyInvalidEvent()
    {
        FakeHttpServer server;
        server.setHandler([](const FakeHttpServer::Request&) {
            return json(400,
                R"json({"status":"invalid event","message":"Event object is invalid",)json"
                R"json("errors":["Length of 'routing_key' is incorrect (should be 32 characters)"]})json");
        });
        PagerDutyNotifier notifier(pagerDuty(server));
        QString error;
        QVERIFY(!deliver(notifier, sampleEvent(), &error));
        QCOMPARE(error,
            QStringLiteral("PagerDuty: Length of 'routing_key' is incorrect (should be 32 characters)"));
        QCOMPARE(server.requests().size(), 1);  // no point trying again
    }

    void pagerDutyRetriesWhenRateLimited()
    {
        FakeHttpServer server;
        int calls = 0;
        server.setHandler(
            [&calls](const FakeHttpServer::Request&) { return json(++calls == 1 ? 429 : 202, "{}"); });
        PagerDutyNotifier notifier(pagerDuty(server));
        QVERIFY(deliver(notifier, sampleEvent()));
        QCOMPARE(server.requests().size(), 2);
    }

    // Resolving goes one alert at a time and stops at the first refusal.
    void pagerDutyStopsAtAFailedResolve()
    {
        FakeHttpServer server;
        int calls = 0;
        server.setHandler([&calls](const FakeHttpServer::Request&) {
            return json(++calls == 2 ? 400 : 202, R"({"message":"Event object is invalid"})");
        });
        PagerDutyNotifier notifier(pagerDuty(server));
        auto event = sampleEvent();
        event.type = EventType::PowerRestored;
        QString error;
        QVERIFY(!deliver(notifier, event, &error));
        QCOMPARE(error, QStringLiteral("PagerDuty: Event object is invalid"));
        QTest::qWait(50);
        QCOMPARE(server.requests().size(), 2);
    }

    void pagerDutyLongSummaryIsCut()
    {
        FakeHttpServer server;
        server.setHandler([](const FakeHttpServer::Request&) { return json(202, "{}"); });
        PagerDutyNotifier notifier(pagerDuty(server));
        auto event = sampleEvent();
        event.message = QString(3000, u'x');
        QVERIFY(deliver(notifier, event));
        const auto body = QJsonDocument::fromJson(server.requests().first().body).object();
        const auto summary
            = body.value(QStringLiteral("payload")).toObject().value(QStringLiteral("summary")).toString();
        QCOMPARE(summary.size(), 1024);
        QVERIFY(summary.endsWith(QChar(0x2026)));
    }

    void pagerDutyNeedsKey()
    {
        PagerDutyNotifier notifier(channel(ChannelType::PagerDuty,
            { { QStringLiteral("endpoint"), QStringLiteral("http://127.0.0.1:9/v2/enqueue") } }));
        QString error;
        QVERIFY(!deliver(notifier, sampleEvent(), &error));
        QCOMPARE(error, QStringLiteral("PagerDuty needs an integration key."));
    }

    void placeholders()
    {
        const auto event = sampleEvent();
        const auto values = notify::eventPlaceholders(event);
        const auto rendered = notify::renderTemplate(
            QStringLiteral(
                "{event}|{severity}|{device}|{title}|{message}|{charge}|{runtime}|{runtime_min}|{status}|"
                "{timestamp}|{timestamp_local}"),
            values, false);
        const auto parts = rendered.split(u'|');
        QCOMPARE(parts.size(), 11);
        QCOMPARE(parts.at(0), enumKey(event.type));
        QCOMPARE(parts.at(1), QStringLiteral("warning"));
        QCOMPARE(parts.at(2), event.deviceName);
        QCOMPARE(parts.at(3), event.title);
        QCOMPARE(parts.at(4), event.message);
        QCOMPARE(parts.at(5), QStringLiteral("87"));
        QCOMPARE(parts.at(6), QStringLiteral("1440"));
        QCOMPARE(parts.at(7), QStringLiteral("24"));
        QCOMPARE(parts.at(8), statusTokens(event.flags));
        QCOMPARE(parts.at(9), QStringLiteral("2026-03-04T05:06:07Z"));
        QCOMPARE(parts.at(10), event.timestamp.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));

        Event empty;
        empty.timestamp = event.timestamp;
        const auto missing = notify::renderTemplate(
            QStringLiteral("[{charge}][{runtime}]"), notify::eventPlaceholders(empty), false);
        QCOMPARE(missing, QStringLiteral("[][]"));
    }

    void jsonEscape()
    {
        QCOMPARE(
            notify::jsonEscape(QStringLiteral("a\"b\\c\nd\u0001")), QStringLiteral("a\\\"b\\\\c\\nd\\u0001"));
    }

    void formEncode()
    {
        QCOMPARE(notify::formEncode({ { QStringLiteral("a b"), QStringLiteral("1+1=2&x") } }),
            QByteArray("a%20b=1%2B1%3D2%26x"));
    }
};

QTEST_GUILESS_MAIN(TestHttpNotifiers)
#include "tst_httpnotifiers.moc"
