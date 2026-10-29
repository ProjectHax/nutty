// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/notify/PagerDutyNotifier.h"

#include "core/notify/NotifyUtil.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace nutty {

namespace {

constexpr auto kUsEndpoint = "https://events.pagerduty.com/v2/enqueue";
constexpr auto kEuEndpoint = "https://events.eu.pagerduty.com/v2/enqueue";
// Hidden settings key so tests can point the channel at a local server.
constexpr auto kEndpointKey = "endpoint";
constexpr auto kActionAlert = "action";
constexpr int kMaxSummary = 1024;  // characters, PagerDuty's limit

// Everything that's part of being on battery, ended by line power.
const QList<EventType> kPowerAlerts { EventType::OnBattery, EventType::LowBattery, EventType::ChargeBelow,
    EventType::RuntimeBelow, EventType::ForcedShutdown };

QByteArray toJson(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QJsonObject details(const Event& event)
{
    QJsonObject out;
    if (!event.message.isEmpty())
        out.insert(QStringLiteral("message"), event.message);
    if (event.charge)
        out.insert(QStringLiteral("charge"), qRound(*event.charge * 10.0) / 10.0);
    if (event.runtime)
        out.insert(QStringLiteral("runtime"), double(*event.runtime));
    if (event.load)
        out.insert(QStringLiteral("load"), qRound(*event.load * 10.0) / 10.0);
    for (const char* key : { "action", "ruleName", "reason", "detail" }) {
        const auto value = event.extra.value(QString::fromLatin1(key)).toString();
        if (!value.isEmpty())
            out.insert(QString::fromLatin1(key), value);
    }
    return out;
}

}  // namespace

QString PagerDutyNotifier::severityFor(Severity severity)
{
    return notify::severityKey(severity);
}

QUrl PagerDutyNotifier::endpointFor(const QString& region)
{
    return QUrl(QString::fromLatin1(
        region.trimmed().compare(u"eu", Qt::CaseInsensitive) == 0 ? kEuEndpoint : kUsEndpoint));
}

QString PagerDutyNotifier::alertFor(const Event& event)
{
    switch (event.type) {
    case EventType::Test:
        return {};
    case EventType::ActionScheduled:
    case EventType::ActionExecuted:
    case EventType::ActionCancelled:
        return QString::fromLatin1(kActionAlert);
    case EventType::CommRestored:
        return enumKey(EventType::CommLost);
    default:
        return enumKey(event.type);
    }
}

QStringList PagerDutyNotifier::resolvedBy(const Event& event)
{
    QStringList out;
    switch (event.type) {
    case EventType::PowerRestored:
        for (const auto type : kPowerAlerts)
            out << enumKey(type);
        break;
    case EventType::CommRestored:
        out << enumKey(EventType::CommLost);
        break;
    case EventType::ActionCancelled:
        out << QString::fromLatin1(kActionAlert);
        break;
    case EventType::ActionExecuted:
        // A real one leaves its alert open: the machine went down.
        if (event.extra.value(QStringLiteral("dryRun")).toBool())
            out << QString::fromLatin1(kActionAlert);
        break;
    default:
        break;
    }
    return out;
}

QString PagerDutyNotifier::dedupKey(const QUuid& deviceId, const QString& alert)
{
    if (deviceId.isNull())
        return QStringLiteral("nutty:%1").arg(alert);
    return QStringLiteral("nutty:%1:%2").arg(deviceId.toString(QUuid::WithoutBraces), alert);
}

void PagerDutyNotifier::send(const Event& event)
{
    const auto& settings = config().settings;
    const auto routingKey
        = config().secrets.value(QString::fromLatin1(keys::PagerDutyRoutingKey)).toString().trimmed();
    if (routingKey.isEmpty()) {
        emit failed(channelId(), tr("PagerDuty needs an integration key."));
        return;
    }
    QUrl endpoint = endpointFor(settings.value(QString::fromLatin1(keys::PagerDutyRegion)).toString());
    if (settings.contains(QString::fromLatin1(kEndpointKey)))
        endpoint = QUrl(settings.value(QString::fromLatin1(kEndpointKey)).toString());

    const auto request = [&](const QJsonObject& body) {
        Request r;
        r.request = makeRequest(endpoint);
        r.request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArray("application/json"));
        r.body = toJson(body);
        return r;
    };

    const auto resolves = resolvedBy(event);
    if (!resolves.isEmpty()) {
        QList<Request> requests;
        for (const auto& alert : resolves) {
            requests << request({
                { QStringLiteral("routing_key"), routingKey },
                { QStringLiteral("event_action"), QStringLiteral("resolve") },
                { QStringLiteral("dedup_key"), dedupKey(event.deviceId, alert) },
            });
        }
        submit(std::move(requests));
        return;
    }

    QString summary = event.message.isEmpty() || event.message == event.title
        ? event.title
        : QStringLiteral("%1 — %2").arg(event.title, event.message);
    if (summary.size() > kMaxSummary)
        summary = summary.left(kMaxSummary - 1) + QChar(0x2026);
    const auto ts = event.timestamp.isValid() ? event.timestamp : QDateTime::currentDateTimeUtc();

    QJsonObject payload {
        { QStringLiteral("summary"), summary },
        { QStringLiteral("source"), event.deviceName.isEmpty() ? QStringLiteral("nutty") : event.deviceName },
        { QStringLiteral("severity"), severityFor(event.severity) },
        { QStringLiteral("timestamp"), ts.toUTC().toString(Qt::ISODateWithMs) },
        { QStringLiteral("class"), enumKey(event.type) },
    };
    const auto custom = details(event);
    if (!custom.isEmpty())
        payload.insert(QStringLiteral("custom_details"), custom);

    QJsonObject body {
        { QStringLiteral("routing_key"), routingKey },
        { QStringLiteral("event_action"), QStringLiteral("trigger") },
        { QStringLiteral("client"), QStringLiteral("nutty") },
        { QStringLiteral("client_url"), QStringLiteral(NUTTY_HOMEPAGE) },
        { QStringLiteral("payload"), payload },
    };
    if (const auto alert = alertFor(event); !alert.isEmpty())
        body.insert(QStringLiteral("dedup_key"), dedupKey(event.deviceId, alert));
    submit(request(body));
}

std::optional<QString> PagerDutyNotifier::checkResponse(int httpStatus, const QByteArray& body) const
{
    if (httpStatus >= 200 && httpStatus < 300)
        return std::nullopt;
    // {"status":"invalid event","message":"Event object is invalid","errors":["..."]}
    const auto json = QJsonDocument::fromJson(body).object();
    QStringList parts;
    for (const auto& error : json.value(QStringLiteral("errors")).toArray())
        parts << error.toString();
    if (parts.isEmpty() && !json.value(QStringLiteral("message")).toString().isEmpty())
        parts << json.value(QStringLiteral("message")).toString();
    if (!parts.isEmpty())
        return tr("PagerDuty: %1").arg(parts.join(QStringLiteral("; ")));
    return HttpNotifier::checkResponse(httpStatus, body);
}

bool PagerDutyNotifier::shouldRetry(int httpStatus, bool networkError) const
{
    // 429: too many events at once.
    return httpStatus == 429 || HttpNotifier::shouldRetry(httpStatus, networkError);
}

}  // namespace nutty
