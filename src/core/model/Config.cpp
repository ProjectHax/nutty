// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/model/Config.h"

#include <algorithm>

namespace nutty {

QStringList secretKeys(SourceType type)
{
    switch (type) {
    case SourceType::Nut:
        return { QString::fromLatin1(keys::NutPassword) };
    case SourceType::EcoFlow:
        return { QString::fromLatin1(keys::EcoAccessKey), QString::fromLatin1(keys::EcoSecretKey) };
    case SourceType::HttpJson:
        return { QString::fromLatin1(keys::HttpToken) };
    case SourceType::Apcupsd:
    case SourceType::System:
        break;
    }
    return {};
}

QStringList secretKeys(ChannelType type)
{
    switch (type) {
    case ChannelType::Pushover:
        return { QString::fromLatin1(keys::PushoverUserKey), QString::fromLatin1(keys::PushoverApiToken) };
    case ChannelType::Ntfy:
        return { QString::fromLatin1(keys::NtfyPassword), QString::fromLatin1(keys::NtfyToken) };
    case ChannelType::Email:
        return { QString::fromLatin1(keys::EmailPassword) };
    case ChannelType::Webhook:
        return { QString::fromLatin1(keys::WebhookAuthValue) };
    case ChannelType::PagerDuty:
        return { QString::fromLatin1(keys::PagerDutyRoutingKey) };
    case ChannelType::Desktop:
        break;
    }
    return {};
}

int defaultPollIntervalSec(SourceType type)
{
    switch (type) {
    case SourceType::Nut:
    case SourceType::Apcupsd:
    case SourceType::System:
        return 5;
    case SourceType::HttpJson:
        return 15;
    case SourceType::EcoFlow:
        return 30;  // the cloud API is rate limited
    }
    return 10;
}

bool defaultCommLostCritical(SourceType type)
{
    return type != SourceType::EcoFlow && type != SourceType::HttpJson;
}

int DeviceConfig::effectivePollIntervalSec() const
{
    return pollIntervalSec > 0 ? pollIntervalSec : defaultPollIntervalSec(type);
}

QSet<EventType> defaultChannelEvents(ChannelType type)
{
    QSet<EventType> events {
        EventType::OnBattery,
        EventType::PowerRestored,
        EventType::LowBattery,
        EventType::ChargeBelow,
        EventType::RuntimeBelow,
        EventType::ForcedShutdown,
        EventType::ReplaceBattery,
        EventType::Overload,
        EventType::Alarm,
        EventType::CommLost,
        EventType::CommRestored,
        EventType::AuthFailed,
        EventType::ActionScheduled,
        EventType::ActionCancelled,
        EventType::ActionExecuted,
        EventType::ActionFailed,
        EventType::Test,
    };
    // Remote channels skip the chatty bypass/off transitions by default.
    if (type == ChannelType::Desktop)
        events << EventType::Bypass << EventType::UpsOff;
    return events;
}

namespace {

template <typename T> const T* findById(const QList<T>& list, const QUuid& id)
{
    const auto it = std::ranges::find(list, id, &T::id);
    return it == list.end() ? nullptr : &*it;
}

}  // namespace

const DeviceConfig* AppConfig::device(const QUuid& id) const
{
    return findById(devices, id);
}
const DeviceGroup* AppConfig::group(const QUuid& id) const
{
    return findById(groups, id);
}
const ChannelConfig* AppConfig::channel(const QUuid& id) const
{
    return findById(channels, id);
}

}  // namespace nutty
