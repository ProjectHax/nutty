// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/notify/HttpNotifier.h"

namespace nutty {

// ntfy (ntfy.sh or a self-hosted server): POST <server>/<topic>.
class NtfyNotifier : public HttpNotifier
{
    Q_OBJECT

public:
    using HttpNotifier::HttpNotifier;

    void send(const Event& event) override;

    static int priorityFor(Severity severity);
    static QByteArray tagsFor(const Event& event);
};

}  // namespace nutty
