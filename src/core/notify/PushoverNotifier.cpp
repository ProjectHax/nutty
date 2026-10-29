// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/notify/PushoverNotifier.h"

#include "core/notify/NotifyUtil.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace nutty {

namespace {

constexpr auto kDefaultEndpoint = "https://api.pushover.net/1/messages.json";
// Hidden settings key so tests can point the channel at a local server.
constexpr auto kEndpointKey = "endpoint";

}  // namespace

int PushoverNotifier::priorityFor(Severity severity)
{
    // Never 2 (emergency): that needs retry/expire handling and an
    // acknowledgement flow nutty doesn't offer.
    switch (severity) {
    case Severity::Info:
        return -1;
    case Severity::Warning:
        return 0;
    case Severity::Critical:
        return 1;
    }
    return 0;
}

void PushoverNotifier::send(const Event& event)
{
    const auto& settings = config().settings;
    const auto& secrets = config().secrets;
    const auto token = secrets.value(QString::fromLatin1(keys::PushoverApiToken)).toString();
    const auto user = secrets.value(QString::fromLatin1(keys::PushoverUserKey)).toString();
    if (token.isEmpty() || user.isEmpty()) {
        emit failed(channelId(), tr("Pushover needs an API token and a user key."));
        return;
    }

    QList<QPair<QString, QString>> fields {
        { QStringLiteral("token"), token },
        { QStringLiteral("user"), user },
        { QStringLiteral("title"), event.title },
        { QStringLiteral("message"), event.message.isEmpty() ? event.title : event.message },
        { QStringLiteral("priority"), QString::number(priorityFor(event.severity)) },
    };
    if (event.timestamp.isValid())
        fields.append({ QStringLiteral("timestamp"), QString::number(event.timestamp.toSecsSinceEpoch()) });
    const auto device = settings.value(QString::fromLatin1(keys::PushoverDevice)).toString();
    if (!device.isEmpty())
        fields.append({ QStringLiteral("device"), device });
    const auto sound = settings.value(QString::fromLatin1(keys::PushoverSound)).toString();
    if (!sound.isEmpty())
        fields.append({ QStringLiteral("sound"), sound });

    const auto endpoint
        = settings.value(QString::fromLatin1(kEndpointKey), QString::fromLatin1(kDefaultEndpoint)).toString();
    Request request;
    request.request = makeRequest(QUrl(endpoint));
    request.request.setHeader(
        QNetworkRequest::ContentTypeHeader, QByteArray("application/x-www-form-urlencoded"));
    request.body = notify::formEncode(fields);
    submit(std::move(request));
}

std::optional<QString> PushoverNotifier::checkResponse(int httpStatus, const QByteArray& body) const
{
    const auto json = QJsonDocument::fromJson(body).object();
    if (httpStatus >= 200 && httpStatus < 300 && json.value(QStringLiteral("status")).toInt() == 1)
        return std::nullopt;

    QStringList errors;
    for (const auto& error : json.value(QStringLiteral("errors")).toArray())
        errors << error.toString();
    if (!errors.isEmpty())
        return tr("Pushover: %1").arg(errors.join(QStringLiteral("; ")));
    return HttpNotifier::checkResponse(httpStatus >= 200 && httpStatus < 300 ? 502 : httpStatus, body);
}

}  // namespace nutty
