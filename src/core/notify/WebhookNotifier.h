// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/notify/HttpNotifier.h"

namespace nutty {

// POST/PUT to any URL. The body comes from a preset (json, discord, slack,
// teams) or from a custom template with {placeholders}.
class WebhookNotifier : public HttpNotifier
{
    Q_OBJECT

public:
    using HttpNotifier::HttpNotifier;

    void send(const Event& event) override;

    struct Body {
        QByteArray contentType;
        QByteArray data;
    };
    // Builds the request body for a preset; "custom" renders tmpl. A custom
    // template that starts with '{' (but not a {placeholder}) or '[' is JSON and
    // gets JSON-escaped values.
    static Body buildBody(const QString& preset, const QString& tmpl, const Event& event);

protected:
    bool shouldRetry(int httpStatus, bool networkError) const override;
};

}  // namespace nutty
