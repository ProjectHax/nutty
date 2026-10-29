// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <utility>

// Request signing for the EcoFlow IoT Open Platform. Every request carries the
// headers accessKey, nonce, timestamp (ms) and sign, where sign is the hex
// HMAC-SHA256 (keyed with the secret key) of:
//
//   <flattened request params, sorted by key>&accessKey=..&nonce=..&timestamp=..
//
// Nested objects flatten to "a.b", arrays to "a[0]". The three trailing pairs
// are appended in that fixed order, not sorted in with the params.
namespace nutty::ecoflow {

using Param = std::pair<QString, QString>;

// Flattened, unsorted.
QList<Param> flatten(const QJsonObject& params);

// The exact byte string that gets signed.
QByteArray signaturePayload(
    QList<Param> params, const QString& accessKey, const QString& nonce, const QString& timestamp);

QString hmacSha256Hex(const QByteArray& payload, const QString& secretKey);

struct Signature {
    QString accessKey;
    QString nonce;
    QString timestamp;
    QString sign;
};

Signature signRequest(const QJsonObject& params, const QString& accessKey, const QString& secretKey,
    const QString& nonce, const QString& timestamp);
// Same, with a random 6-digit nonce and the current time.
Signature signRequest(const QJsonObject& params, const QString& accessKey, const QString& secretKey);

}  // namespace nutty::ecoflow
