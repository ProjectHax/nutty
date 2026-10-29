// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Config.h"
#include "core/model/Event.h"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>

namespace nutty {

// Delivers events over one notification channel (desktop, Pushover, ntfy,
// email, webhook, PagerDuty). Sending is async; exactly one of
// sent()/failed() follows each send().
class Notifier : public QObject
{
    Q_OBJECT

public:
    explicit Notifier(ChannelConfig config, QObject* parent = nullptr);
    ~Notifier() override;

    const ChannelConfig& config() const { return m_config; }
    QUuid channelId() const { return m_config.id; }

    virtual void send(const Event& event) = 0;

signals:
    void sent(const QUuid& channelId);
    void failed(const QUuid& channelId, const QString& error);
    // The user picked an action on a notification (e.g. "cancel" on a
    // shutdown countdown). Only the desktop notifier on Linux emits this.
    void actionInvoked(const QString& actionId);

private:
    ChannelConfig m_config;
};

// Creates notifiers by channel type. nutty_notify registers the network
// channels, nutty_platform registers Desktop; tests register fakes.
class NotifierRegistry
{
public:
    using Factory = std::function<std::unique_ptr<Notifier>(const ChannelConfig&, QObject* parent)>;

    static void registerFactory(ChannelType type, Factory factory);
    static bool hasFactory(ChannelType type);
    static std::unique_ptr<Notifier> create(const ChannelConfig& config, QObject* parent = nullptr);
};

}  // namespace nutty
