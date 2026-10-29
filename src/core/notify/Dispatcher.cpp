// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/notify/Dispatcher.h"

#include "core/model/Notifier.h"

#include <QLoggingCategory>

namespace nutty {

Q_LOGGING_CATEGORY(lcDispatch, "nutty.notify")

namespace {

// Whether a running notifier can keep serving an updated channel config. Only
// routing (events) may change without recreating it.
bool sameDelivery(const ChannelConfig& a, const ChannelConfig& b)
{
    return a.type == b.type && a.name == b.name && a.settings == b.settings && a.secrets == b.secrets;
}

}  // namespace

Dispatcher::Dispatcher(QObject* parent)
    : QObject(parent)
{
}

Dispatcher::~Dispatcher() = default;

void Dispatcher::applyConfig(const AppConfig& config)
{
    // Drop notifiers for removed, disabled or changed channels.
    for (auto it = m_notifiers.begin(); it != m_notifiers.end();) {
        const auto* channel = config.channel(it.key());
        if (!channel || !channel->enabled || !sameDelivery(*channel, it.value()->config())) {
            it.value()->disconnect(this);
            it.value()->deleteLater();
            it = m_notifiers.erase(it);
        } else {
            ++it;
        }
    }

    for (const auto& channel : config.channels) {
        if (!channel.enabled || m_notifiers.contains(channel.id))
            continue;
        auto notifier = NotifierRegistry::create(channel, this);
        if (!notifier) {
            qCWarning(lcDispatch) << "no notifier for channel type" << enumKey(channel.type);
            continue;
        }
        auto* raw = notifier.release();
        connect(raw, &Notifier::sent, this, &Dispatcher::channelSent);
        connect(raw, &Notifier::failed, this, &Dispatcher::channelFailed);
        connect(raw, &Notifier::actionInvoked, this, &Dispatcher::actionInvoked);
        m_notifiers.insert(channel.id, raw);
    }
    m_config = config;
}

bool Dispatcher::routes(const ChannelConfig& channel, const Event& event, const AppConfig& config)
{
    if (!channel.enabled || !channel.events.contains(event.type))
        return false;
    if (event.deviceId.isNull())
        return true;
    const auto* device = config.device(event.deviceId);
    return !device || device->channels.isEmpty() || device->channels.contains(channel.id);
}

void Dispatcher::dispatch(const Event& event)
{
    for (const auto& channel : std::as_const(m_config.channels)) {
        auto* notifier = m_notifiers.value(channel.id);
        if (notifier && routes(channel, event, m_config))
            notifier->send(event);
    }
}

Event Dispatcher::testEvent()
{
    Event event;
    event.type = EventType::Test;
    event.severity = Severity::Info;
    event.timestamp = QDateTime::currentDateTimeUtc();
    event.deviceName = QStringLiteral("nutty");
    event.title = tr("Test notification");
    event.message = tr("If you can read this, nutty can reach you here.");
    return event;
}

void Dispatcher::sendTest(const ChannelConfig& channel)
{
    auto notifier = NotifierRegistry::create(channel, this);
    if (!notifier) {
        emit testFinished(channel.id, false, tr("This kind of channel is not available on this system."));
        return;
    }
    auto* raw = notifier.release();
    const auto id = channel.id;
    connect(raw, &Notifier::sent, this, [this, raw, id]() {
        emit testFinished(id, true, {});
        raw->deleteLater();
    });
    connect(raw, &Notifier::failed, this, [this, raw, id](const QUuid&, const QString& error) {
        emit testFinished(id, false, error);
        raw->deleteLater();
    });
    raw->send(testEvent());
}

}  // namespace nutty
