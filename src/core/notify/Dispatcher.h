// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Config.h"
#include "core/model/Event.h"

#include <QHash>
#include <QObject>
#include <QUuid>

namespace nutty {

class Notifier;

// Routes events to the configured notification channels. Notifiers are
// created through NotifierRegistry, so every channel type whose factory is
// registered (core channels via registerCoreNotifiers(), Desktop via the
// platform) is supported.
class Dispatcher : public QObject
{
    Q_OBJECT

public:
    explicit Dispatcher(QObject* parent = nullptr);
    ~Dispatcher() override;

    // Creates, keeps or drops notifiers to match the enabled channels.
    void applyConfig(const AppConfig& config);
    void dispatch(const Event& event);
    // Sends a test notification through a channel that need not be saved yet.
    void sendTest(const ChannelConfig& channel);

    // Whether event goes to channel: the channel is enabled, subscribes to the
    // event type, and (for device events) the device routes to it.
    static bool routes(const ChannelConfig& channel, const Event& event, const AppConfig& config);
    // A ready-made test event.
    static Event testEvent();

    QList<QUuid> activeChannels() const { return m_notifiers.keys(); }

signals:
    void channelSent(const QUuid& channelId);
    void channelFailed(const QUuid& channelId, const QString& error);
    void testFinished(const QUuid& channelId, bool ok, const QString& error);
    // Forwarded from a notifier (e.g. "cancel" or "open" from a desktop notification).
    void actionInvoked(const QString& actionId);

private:
    AppConfig m_config;
    QHash<QUuid, Notifier*> m_notifiers;
};

}  // namespace nutty
