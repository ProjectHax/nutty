// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Notifier.h"
#include "core/notify/SmtpClient.h"

#include <QQueue>

namespace nutty {

// Sends each event as a plain-text email through SmtpClient. Events that
// arrive while a message is in flight are queued.
class EmailNotifier : public Notifier
{
    Q_OBJECT

public:
    explicit EmailNotifier(ChannelConfig config, QObject* parent = nullptr);

    void send(const Event& event) override;

    // Connection settings derived from the channel config (port defaults
    // follow the security mode: 25 / 587 / 465).
    static SmtpClient::Settings smtpSettings(const ChannelConfig& config);
    static SmtpClient::Message message(const ChannelConfig& config, const Event& event);

private:
    void sendNext();

    SmtpClient* m_client;
    QQueue<Event> m_queue;
};

}  // namespace nutty
