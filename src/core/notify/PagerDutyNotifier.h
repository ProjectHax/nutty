// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/notify/HttpNotifier.h"

#include <QStringList>
#include <QUrl>

namespace nutty {

// PagerDuty Events API v2. Each kind of trouble on a device is an alert of
// its own, so a low battery pages again during an outage that already paged;
// the same trouble again adds to its open alert instead. Recoveries resolve
// what they end: power restored the device's power alerts, contact restored
// its lost-contact alert, a cancelled (or simulated) action its action alert.
class PagerDutyNotifier : public HttpNotifier
{
    Q_OBJECT

public:
    using HttpNotifier::HttpNotifier;

    void send(const Event& event) override;

    // "info" | "warning" | "critical"
    static QString severityFor(Severity severity);
    // The enqueue URL for a region setting ("us", the default, or "eu").
    static QUrl endpointFor(const QString& region);
    // The alert an event opens or adds to, e.g. "OnBattery" or "action";
    // empty for a test, which always opens a new one.
    static QString alertFor(const Event& event);
    // The alerts an event resolves; empty when it opens or adds to one.
    static QStringList resolvedBy(const Event& event);
    // Identifies an alert to PagerDuty, e.g. "nutty:<device id>:OnBattery".
    static QString dedupKey(const QUuid& deviceId, const QString& alert);

protected:
    std::optional<QString> checkResponse(int httpStatus, const QByteArray& body) const override;
    bool shouldRetry(int httpStatus, bool networkError) const override;
};

}  // namespace nutty
