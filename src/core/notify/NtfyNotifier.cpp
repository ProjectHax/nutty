// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/notify/NtfyNotifier.h"

#include "core/notify/NotifyUtil.h"

#include <QUrl>

namespace nutty {

namespace {

constexpr auto kDefaultServer = "https://ntfy.sh";

}  // namespace

int NtfyNotifier::priorityFor(Severity severity)
{
    switch (severity) {
    case Severity::Info:
        return 3;
    case Severity::Warning:
        return 4;
    case Severity::Critical:
        return 5;
    }
    return 3;
}

QByteArray NtfyNotifier::tagsFor(const Event& event)
{
    // ntfy turns known tag names into emoji in front of the title.
    switch (event.type) {
    case EventType::OnBattery:
    case EventType::ChargeBelow:
    case EventType::RuntimeBelow:
        return "battery,warning";
    case EventType::PowerRestored:
        return "electric_plug,white_check_mark";
    case EventType::LowBattery:
    case EventType::ForcedShutdown:
    case EventType::ActionScheduled:
    case EventType::ActionExecuted:
    case EventType::ActionFailed:
        return "rotating_light";
    case EventType::CommLost:
    case EventType::AuthFailed:
        return "warning";
    case EventType::CommRestored:
    case EventType::ActionCancelled:
        return "white_check_mark";
    case EventType::Test:
        return "test_tube";
    default:
        return event.severity == Severity::Info ? QByteArray("information_source") : QByteArray("warning");
    }
}

void NtfyNotifier::send(const Event& event)
{
    const auto& settings = config().settings;
    const auto& secrets = config().secrets;
    auto server = settings.value(QString::fromLatin1(keys::NtfyServer)).toString().trimmed();
    if (server.isEmpty())
        server = QString::fromLatin1(kDefaultServer);
    while (server.endsWith(u'/'))
        server.chop(1);
    const auto topic = settings.value(QString::fromLatin1(keys::NtfyTopic)).toString().trimmed();
    if (topic.isEmpty()) {
        emit failed(channelId(), tr("ntfy needs a topic."));
        return;
    }

    const QUrl url(server + u'/' + QString::fromUtf8(QUrl::toPercentEncoding(topic)));
    if (!url.isValid() || url.scheme().isEmpty()) {
        emit failed(channelId(), tr("The ntfy server address is not a valid URL."));
        return;
    }

    Request request;
    request.request = makeRequest(url);
    request.request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArray("text/plain; charset=utf-8"));
    // Header values are Latin-1 on the wire; ntfy decodes RFC 2047 words.
    request.request.setRawHeader("Title", notify::encodeHeaderWord(event.title).replace("\r\n ", " "));
    request.request.setRawHeader("Priority", QByteArray::number(priorityFor(event.severity)));
    request.request.setRawHeader("Tags", tagsFor(event));

    const auto token = secrets.value(QString::fromLatin1(keys::NtfyToken)).toString();
    const auto username = settings.value(QString::fromLatin1(keys::NtfyUsername)).toString();
    const auto password = secrets.value(QString::fromLatin1(keys::NtfyPassword)).toString();
    if (!token.isEmpty())
        request.request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    else if (!username.isEmpty())
        request.request.setRawHeader(
            "Authorization", "Basic " + (username + u':' + password).toUtf8().toBase64());

    request.body = (event.message.isEmpty() ? event.title : event.message).toUtf8();
    submit(std::move(request));
}

}  // namespace nutty
