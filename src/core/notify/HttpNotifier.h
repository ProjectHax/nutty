// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Notifier.h"

#include <QByteArray>
#include <QNetworkRequest>

#include <optional>

class QNetworkAccessManager;

namespace nutty {

// Base for channels that deliver over HTTP (Pushover, ntfy, webhooks,
// PagerDuty): submits the request, checks the response and retries once when
// shouldRetry() allows it.
class HttpNotifier : public Notifier
{
    Q_OBJECT

public:
    explicit HttpNotifier(ChannelConfig config, QObject* parent = nullptr);
    ~HttpNotifier() override;

protected:
    struct Request {
        QNetworkRequest request;
        QByteArray verb = "POST";
        QByteArray body;
    };

    // An error message to fail with, or std::nullopt on success. The default
    // accepts any 2xx.
    virtual std::optional<QString> checkResponse(int httpStatus, const QByteArray& body) const;
    // Whether a failed attempt is worth one more try. The default retries
    // network errors and 5xx responses.
    virtual bool shouldRetry(int httpStatus, bool networkError) const;

    void submit(Request request);
    // Several requests, one after the other: sent() once the last has gone
    // through, failed() at the first that doesn't (the rest aren't sent).
    void submit(QList<Request> requests);
    // Starts a request with nutty's User-Agent and a transfer timeout.
    static QNetworkRequest makeRequest(const QUrl& url);

private:
    void attempt(Request request, int attemptNumber, QList<Request> rest);

    QNetworkAccessManager* m_network;
};

}  // namespace nutty
