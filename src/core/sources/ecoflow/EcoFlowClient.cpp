// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/sources/ecoflow/EcoFlowClient.h"

#include "core/model/Config.h"
#include "core/sources/ecoflow/EcoFlowSigner.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

namespace nutty::ecoflow {

EcoFlowClient::EcoFlowClient(const QVariantMap& settings, const QVariantMap& secrets, QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_base(baseUrl(settings))
    , m_accessKey(secrets.value(QLatin1StringView(keys::EcoAccessKey)).toString().trimmed())
    , m_secretKey(secrets.value(QLatin1StringView(keys::EcoSecretKey)).toString().trimmed())
{
}

QString EcoFlowClient::baseUrl(const QVariantMap& settings)
{
    const QString region = settings.value(QLatin1StringView(keys::EcoRegion)).toString();
    if (region == u"eu")
        return QStringLiteral("https://api-e.ecoflow.com");
    if (region == u"custom") {
        QString host = settings.value(QLatin1StringView(keys::EcoHost)).toString().trimmed();
        while (host.endsWith(u'/'))
            host.chop(1);
        if (!host.isEmpty())
            return host.contains(u"://") ? host : QStringLiteral("https://") + host;
    }
    return QStringLiteral("https://api.ecoflow.com");
}

void EcoFlowClient::get(const QString& path, const QJsonObject& params, Callback callback)
{
    QUrl url(m_base + path);
    QUrlQuery query;
    for (const auto& [key, value] : flatten(params))
        query.addQueryItem(key, value);
    url.setQuery(query);
    handle(m_nam->get(signedRequest(url, params)), std::move(callback));
}

void EcoFlowClient::put(const QString& path, const QJsonObject& body, Callback callback)
{
    QNetworkRequest request = signedRequest(QUrl(m_base + path), body);
    request.setHeader(
        QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json;charset=UTF-8"));
    handle(m_nam->put(request, QJsonDocument(body).toJson(QJsonDocument::Compact)), std::move(callback));
}

QNetworkRequest EcoFlowClient::signedRequest(const QUrl& url, const QJsonObject& params) const
{
    const Signature sig = signRequest(params, m_accessKey, m_secretKey);
    QNetworkRequest request(url);
    request.setRawHeader("accessKey", sig.accessKey.toUtf8());
    request.setRawHeader("nonce", sig.nonce.toUtf8());
    request.setRawHeader("timestamp", sig.timestamp.toUtf8());
    request.setRawHeader("sign", sig.sign.toUtf8());
    request.setTransferTimeout(kTimeoutMs);
    // Qt keeps an HTTP/2 connection open when a request on it times out, so
    // once EcoFlow stops serving a connection (after hours of polling) every
    // request queued on it times out too, until the server closes it about
    // ten minutes later. Over HTTP/1.1 the timed-out request takes its
    // connection down and the next one starts afresh.
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    return request;
}

void EcoFlowClient::handle(QNetworkReply* reply, Callback callback)
{
    connect(reply, &QNetworkReply::finished, this, [reply, callback = std::move(callback)] {
        reply->deleteLater();
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // A reply that timed out is closed already.
        const QByteArray body = reply->isOpen() ? reply->readAll() : QByteArray();

        if (http == 401 || http == 403) {
            callback(std::unexpected(Error { SourceError::Auth, tr("EcoFlow rejected the access key") }));
            return;
        }
        if (reply->error() != QNetworkReply::NoError && body.isEmpty()) {
            const bool tls = reply->error() == QNetworkReply::SslHandshakeFailedError;
            callback(std::unexpected(
                Error { tls ? SourceError::Tls : SourceError::Network, reply->errorString() }));
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
        if (!doc.isObject()) {
            callback(
                std::unexpected(Error { SourceError::Protocol, tr("EcoFlow sent an unreadable response") }));
            return;
        }
        const QJsonObject root = doc.object();
        // "code" is the string "0" on success (some endpoints send the number).
        const QJsonValue code = root.value(u"code");
        const QString codeText = code.isDouble() ? QString::number(code.toInt()) : code.toString();
        if (codeText == u"0") {
            callback(root.value(u"data"));
            return;
        }

        const QString message = root.value(u"message").toString();
        const QString lower = message.toLower();
        SourceError kind = SourceError::Protocol;
        if (lower.contains(u"sign") || lower.contains(u"accesskey") || lower.contains(u"auth")
            || lower.contains(u"permission"))
            kind = SourceError::Auth;
        else if (lower.contains(u"offline"))
            kind = SourceError::Network;
        else if (lower.contains(u"not exist") || lower.contains(u"not found") || lower.contains(u"not bound")
            || lower.contains(u"not belong"))
            kind = SourceError::NotFound;
        callback(std::unexpected(Error { kind,
            message.isEmpty() ? tr("EcoFlow error %1").arg(codeText)
                              : tr("EcoFlow: %1 (code %2)").arg(message, codeText) }));
    });
}

}  // namespace nutty::ecoflow
