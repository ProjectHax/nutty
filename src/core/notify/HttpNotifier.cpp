// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/notify/HttpNotifier.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>

namespace nutty {

namespace {

constexpr int kTransferTimeoutMs = 20000;
constexpr int kMaxAttempts = 2;
// Settings key (tests only) overriding the delay before the retry.
constexpr auto kRetryDelayKey = "retryDelayMs";
constexpr int kDefaultRetryDelayMs = 2000;

}  // namespace

HttpNotifier::HttpNotifier(ChannelConfig config, QObject* parent)
    : Notifier(std::move(config), parent)
    , m_network(new QNetworkAccessManager(this))
{
}

HttpNotifier::~HttpNotifier() = default;

std::optional<QString> HttpNotifier::checkResponse(int httpStatus, const QByteArray& body) const
{
    if (httpStatus >= 200 && httpStatus < 300)
        return std::nullopt;
    const auto snippet = QString::fromUtf8(body.left(200)).simplified();
    return snippet.isEmpty() ? tr("HTTP %1").arg(httpStatus) : tr("HTTP %1: %2").arg(httpStatus).arg(snippet);
}

bool HttpNotifier::shouldRetry(int httpStatus, bool networkError) const
{
    return networkError || httpStatus >= 500;
}

QNetworkRequest HttpNotifier::makeRequest(const QUrl& url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QByteArray("nutty/" NUTTY_VERSION));
    request.setTransferTimeout(kTransferTimeoutMs);
    return request;
}

void HttpNotifier::submit(Request request)
{
    attempt(std::move(request), 1, {});
}

void HttpNotifier::submit(QList<Request> requests)
{
    if (requests.isEmpty()) {
        emit sent(channelId());
        return;
    }
    Request first = requests.takeFirst();
    attempt(std::move(first), 1, std::move(requests));
}

void HttpNotifier::attempt(Request request, int attemptNumber, QList<Request> rest)
{
    auto* reply = m_network->sendCustomRequest(request.request, request.verb, request.body);
    connect(reply, &QNetworkReply::finished, this,
        [this, reply, request = std::move(request), attemptNumber, rest = std::move(rest)]() {
            reply->deleteLater();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray body = reply->readAll();
            const bool networkError = status == 0;

            std::optional<QString> error;
            if (networkError)
                error = reply->errorString();
            else
                error = checkResponse(status, body);

            if (!error) {
                if (rest.isEmpty())
                    emit sent(channelId());
                else
                    submit(rest);
                return;
            }
            if (attemptNumber < kMaxAttempts && shouldRetry(status, networkError)) {
                const int delay
                    = config()
                          .settings.value(QString::fromLatin1(kRetryDelayKey), kDefaultRetryDelayMs)
                          .toInt();
                QTimer::singleShot(delay, this,
                    [this, request, attemptNumber, rest]() { attempt(request, attemptNumber + 1, rest); });
                return;
            }
            emit failed(channelId(), *error);
        });
}

}  // namespace nutty
