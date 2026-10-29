// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Source.h"

#include <QHash>
#include <QJsonValue>
#include <QNetworkRequest>
#include <QStringList>
#include <QTimer>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace nutty {

// Polls a generic HTTP endpoint that returns JSON and maps fields onto a
// DeviceStatus through configurable paths. The "homeassistant" preset instead
// reads one Home Assistant entity per mapped field (GET /api/states/<entity>).
class HttpJsonSource : public Source
{
    Q_OBJECT

public:
    explicit HttpJsonSource(DeviceConfig config, QObject* parent = nullptr);
    ~HttpJsonSource() override;

    void start() override;
    void stop() override;
    void pollNow() override;
    QString describe() const override;

    static constexpr int kTimeoutMs = 10000;

    // The mappable field names (keys:: HttpCharge ... HttpStatus).
    static QStringList fieldNames();

    // Builds a status from resolved field values (field name -> JSON value).
    // `runtimeUnitHint` is a unit for the runtime value (Home Assistant's
    // unit_of_measurement) used when the runtimeUnit setting is empty.
    static DeviceStatus buildStatus(const QHash<QString, QJsonValue>& values, const QVariantMap& settings,
        const QString& runtimeUnitHint = {});

private:
    void poll();
    void pollGeneric();
    void pollHomeAssistant();
    QNetworkRequest makeRequest(const QUrl& url) const;
    QNetworkReply* send(const QUrl& url);
    void finishHomeAssistant();

    QNetworkAccessManager* m_nam = nullptr;
    QTimer m_pollTimer;
    bool m_running = false;
    bool m_homeAssistant = false;
    QString m_url;
    QList<std::pair<QByteArray, QByteArray>> m_headers;
    bool m_insecureTls = false;

    // Home Assistant poll in progress.
    int m_pending = 0;
    QHash<QString, QJsonValue> m_haValues;
    QString m_haRuntimeUnit;
    SourceError m_haErrorKind = SourceError::None;
    QString m_haError;
};

}  // namespace nutty
