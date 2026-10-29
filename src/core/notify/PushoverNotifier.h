// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/notify/HttpNotifier.h"

namespace nutty {

// Pushover (pushover.net) messages API.
class PushoverNotifier : public HttpNotifier
{
    Q_OBJECT

public:
    using HttpNotifier::HttpNotifier;

    void send(const Event& event) override;

    static int priorityFor(Severity severity);

protected:
    std::optional<QString> checkResponse(int httpStatus, const QByteArray& body) const override;
};

}  // namespace nutty
