// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Types.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QVariantMap>

#include <expected>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
class QUrl;

namespace nutty::ecoflow {

// Signed requests against the EcoFlow IoT Open Platform REST API.
class EcoFlowClient : public QObject
{
    Q_OBJECT

public:
    struct Error {
        SourceError kind = SourceError::Network;
        QString message;
    };
    using Result = std::expected<QJsonValue, Error>;
    using Callback = std::function<void(const Result& result)>;

    static constexpr int kTimeoutMs = 15000;

    EcoFlowClient(const QVariantMap& settings, const QVariantMap& secrets, QObject* parent = nullptr);

    // https://api.ecoflow.com, https://api-e.ecoflow.com, or the custom host.
    static QString baseUrl(const QVariantMap& settings);

    bool hasCredentials() const { return !m_accessKey.isEmpty() && !m_secretKey.isEmpty(); }
    QString base() const { return m_base; }

    // GET <base><path>?<params> with the signature headers. The callback gets
    // the response's "data" member when "code" is "0".
    void get(const QString& path, const QJsonObject& params, Callback callback);
    // PUT <base><path> with `body` as JSON, signed over the body's fields.
    void put(const QString& path, const QJsonObject& body, Callback callback);

private:
    QNetworkRequest signedRequest(const QUrl& url, const QJsonObject& params) const;
    void handle(QNetworkReply* reply, Callback callback);

    QNetworkAccessManager* m_nam = nullptr;
    QString m_base;
    QString m_accessKey;
    QString m_secretKey;
};

}  // namespace nutty::ecoflow
