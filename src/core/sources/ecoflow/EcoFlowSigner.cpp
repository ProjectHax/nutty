// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/sources/ecoflow/EcoFlowSigner.h"

#include <QDateTime>
#include <QJsonArray>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>

#include <algorithm>
#include <cmath>

namespace nutty::ecoflow {

namespace {

QString scalar(const QJsonValue& v)
{
    switch (v.type()) {
    case QJsonValue::Bool:
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Double: {
        const double d = v.toDouble();
        if (std::isfinite(d) && std::floor(d) == d && std::abs(d) < 9007199254740992.0)
            return QString::number(static_cast<qint64>(d));
        return QString::number(d, 'g', 15);
    }
    case QJsonValue::String:
        return v.toString();
    default:
        return {};
    }
}

void flattenInto(QList<Param>& out, const QString& prefix, const QJsonValue& v)
{
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        for (auto it = o.begin(); it != o.end(); ++it)
            flattenInto(out, prefix.isEmpty() ? it.key() : prefix + u'.' + it.key(), it.value());
    } else if (v.isArray()) {
        const QJsonArray a = v.toArray();
        for (qsizetype i = 0; i < a.size(); ++i)
            flattenInto(out, prefix + u'[' + QString::number(i) + u']', a.at(i));
    } else if (!v.isNull() && !v.isUndefined()) {
        out.append({ prefix, scalar(v) });
    }
}

}  // namespace

QList<Param> flatten(const QJsonObject& params)
{
    QList<Param> out;
    flattenInto(out, QString(), params);
    return out;
}

QByteArray signaturePayload(
    QList<Param> params, const QString& accessKey, const QString& nonce, const QString& timestamp)
{
    std::ranges::sort(params, {}, &Param::first);
    params.append({ QStringLiteral("accessKey"), accessKey });
    params.append({ QStringLiteral("nonce"), nonce });
    params.append({ QStringLiteral("timestamp"), timestamp });

    QByteArray out;
    for (const auto& [key, value] : params) {
        if (!out.isEmpty())
            out += '&';
        out += key.toUtf8() + '=' + value.toUtf8();
    }
    return out;
}

QString hmacSha256Hex(const QByteArray& payload, const QString& secretKey)
{
    return QString::fromLatin1(
        QMessageAuthenticationCode::hash(payload, secretKey.toUtf8(), QCryptographicHash::Sha256).toHex());
}

Signature signRequest(const QJsonObject& params, const QString& accessKey, const QString& secretKey,
    const QString& nonce, const QString& timestamp)
{
    Signature s;
    s.accessKey = accessKey;
    s.nonce = nonce;
    s.timestamp = timestamp;
    s.sign = hmacSha256Hex(signaturePayload(flatten(params), accessKey, nonce, timestamp), secretKey);
    return s;
}

Signature signRequest(const QJsonObject& params, const QString& accessKey, const QString& secretKey)
{
    const QString nonce = QString::number(QRandomGenerator::global()->bounded(100000, 1000000));
    const QString timestamp = QString::number(QDateTime::currentMSecsSinceEpoch());
    return signRequest(params, accessKey, secretKey, nonce, timestamp);
}

}  // namespace nutty::ecoflow
