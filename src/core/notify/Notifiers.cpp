// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/notify/Notifiers.h"

#include "core/notify/EmailNotifier.h"
#include "core/notify/NtfyNotifier.h"
#include "core/notify/PagerDutyNotifier.h"
#include "core/notify/PushoverNotifier.h"
#include "core/notify/WebhookNotifier.h"

namespace nutty {

namespace {

template <typename T> NotifierRegistry::Factory factory()
{
    return [](const ChannelConfig& config, QObject* parent) -> std::unique_ptr<Notifier> {
        return std::make_unique<T>(config, parent);
    };
}

}  // namespace

void registerCoreNotifiers()
{
    NotifierRegistry::registerFactory(ChannelType::Pushover, factory<PushoverNotifier>());
    NotifierRegistry::registerFactory(ChannelType::Ntfy, factory<NtfyNotifier>());
    NotifierRegistry::registerFactory(ChannelType::Email, factory<EmailNotifier>());
    NotifierRegistry::registerFactory(ChannelType::Webhook, factory<WebhookNotifier>());
    NotifierRegistry::registerFactory(ChannelType::PagerDuty, factory<PagerDutyNotifier>());
}

}  // namespace nutty
