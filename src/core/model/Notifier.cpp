// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/model/Notifier.h"

#include <QHash>

namespace nutty {

Notifier::Notifier(ChannelConfig config, QObject* parent)
    : QObject(parent)
    , m_config(std::move(config))
{
}

Notifier::~Notifier() = default;

namespace {

QHash<ChannelType, NotifierRegistry::Factory>& notifierFactories()
{
    static QHash<ChannelType, NotifierRegistry::Factory> factories;
    return factories;
}

}  // namespace

void NotifierRegistry::registerFactory(ChannelType type, Factory factory)
{
    notifierFactories().insert(type, std::move(factory));
}

bool NotifierRegistry::hasFactory(ChannelType type)
{
    return notifierFactories().contains(type);
}

std::unique_ptr<Notifier> NotifierRegistry::create(const ChannelConfig& config, QObject* parent)
{
    const auto it = notifierFactories().constFind(config.type);
    if (it == notifierFactories().cend())
        return nullptr;
    return (*it)(config, parent);
}

}  // namespace nutty
