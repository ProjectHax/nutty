// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/notify/WebhookNotifier.h"

#include "core/notify/NotifyUtil.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

namespace nutty {

namespace {

QJsonValue optionalNumber(const std::optional<double>& value)
{
    return value ? QJsonValue(qRound(*value * 10.0) / 10.0) : QJsonValue(QJsonValue::Null);
}

QJsonValue optionalNumber(const std::optional<qint64>& value)
{
    return value ? QJsonValue(double(*value)) : QJsonValue(QJsonValue::Null);
}

QByteArray toJson(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QString themeColor(Severity severity)
{
    switch (severity) {
    case Severity::Info:
        return QStringLiteral("2E7D32");
    case Severity::Warning:
        return QStringLiteral("F9A825");
    case Severity::Critical:
        return QStringLiteral("C62828");
    }
    return QStringLiteral("2E7D32");
}

}  // namespace

WebhookNotifier::Body WebhookNotifier::buildBody(
    const QString& preset, const QString& tmpl, const Event& event)
{
    const auto text = event.message.isEmpty() ? event.title : event.message;
    if (preset == u"discord") {
        return { "application/json",
            toJson({ { QStringLiteral("content"), QStringLiteral("**%1**\n%2").arg(event.title, text) } }) };
    }
    if (preset == u"slack") {
        return { "application/json",
            toJson({ { QStringLiteral("text"), QStringLiteral("*%1*\n%2").arg(event.title, text) } }) };
    }
    if (preset == u"teams") {
        return { "application/json",
            toJson({
                { QStringLiteral("@type"), QStringLiteral("MessageCard") },
                { QStringLiteral("@context"), QStringLiteral("https://schema.org/extensions") },
                { QStringLiteral("summary"), event.title },
                { QStringLiteral("title"), event.title },
                { QStringLiteral("text"), text },
                { QStringLiteral("themeColor"), themeColor(event.severity) },
            }) };
    }
    if (preset == u"custom") {
        // "{device} is ..." starts with a placeholder, not a JSON object.
        static const QRegularExpression leadingPlaceholder(QStringLiteral("^\\{[a-z_]+\\}"));
        const auto trimmed = tmpl.trimmed();
        const bool json = trimmed.startsWith(u'[')
            || (trimmed.startsWith(u'{') && !leadingPlaceholder.match(trimmed).hasMatch());
        const auto rendered = notify::renderTemplate(tmpl, notify::eventPlaceholders(event), json);
        return { json ? QByteArray("application/json") : QByteArray("text/plain; charset=utf-8"),
            rendered.toUtf8() };
    }

    // "json" (the default): a flat, documented payload.
    const auto ts = event.timestamp.isValid() ? event.timestamp : QDateTime::currentDateTimeUtc();
    return { "application/json",
        toJson({
            { QStringLiteral("event"), enumKey(event.type) },
            { QStringLiteral("severity"), notify::severityKey(event.severity) },
            { QStringLiteral("device"), event.deviceName },
            { QStringLiteral("deviceId"),
                event.deviceId.isNull() ? QJsonValue(QJsonValue::Null)
                                        : QJsonValue(event.deviceId.toString(QUuid::WithoutBraces)) },
            { QStringLiteral("title"), event.title },
            { QStringLiteral("message"), event.message },
            { QStringLiteral("charge"), optionalNumber(event.charge) },
            { QStringLiteral("runtime"), optionalNumber(event.runtime) },
            { QStringLiteral("load"), optionalNumber(event.load) },
            { QStringLiteral("status"), statusTokens(event.flags) },
            { QStringLiteral("timestamp"), ts.toUTC().toString(Qt::ISODate) },
        }) };
}

void WebhookNotifier::send(const Event& event)
{
    const auto& settings = config().settings;
    const QUrl url(settings.value(QString::fromLatin1(keys::WebhookUrl)).toString().trimmed());
    if (!url.isValid() || (url.scheme() != u"https" && url.scheme() != u"http")) {
        emit failed(channelId(), tr("The webhook needs an http:// or https:// URL."));
        return;
    }

    const auto preset
        = settings.value(QString::fromLatin1(keys::WebhookPreset), QStringLiteral("json")).toString();
    const auto tmpl = settings.value(QString::fromLatin1(keys::WebhookTemplate)).toString();
    const auto body = buildBody(preset, tmpl, event);

    Request request;
    request.request = makeRequest(url);
    request.request.setHeader(QNetworkRequest::ContentTypeHeader, body.contentType);
    for (const auto& [name, value] :
        notify::parseHeaderLines(settings.value(QString::fromLatin1(keys::WebhookHeaders)).toString()))
        request.request.setRawHeader(name, value);
    const auto authHeader = settings.value(QString::fromLatin1(keys::WebhookAuthHeader)).toString().trimmed();
    const auto authValue = config().secrets.value(QString::fromLatin1(keys::WebhookAuthValue)).toString();
    if (!authHeader.isEmpty() && !authValue.isEmpty())
        request.request.setRawHeader(authHeader.toLatin1(), authValue.toUtf8());

    const auto method = settings.value(QString::fromLatin1(keys::WebhookMethod), QStringLiteral("POST"))
                            .toString()
                            .toUpper();
    request.verb = method == u"PUT" ? QByteArray("PUT") : QByteArray("POST");
    request.body = body.data;
    submit(std::move(request));
}

bool WebhookNotifier::shouldRetry(int, bool) const
{
    // Any failure gets one more try: receivers are often small home servers
    // that hiccup during the very outage being reported.
    return true;
}

}  // namespace nutty
