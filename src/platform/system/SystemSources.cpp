// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/system/SystemSources.h"

#include "platform/system/SystemPowerSource.h"

namespace nutty {

void registerSystemSource()
{
    SourceRegistry::registerFactory(
        SourceType::System, [](const DeviceConfig& config, QObject* parent) -> std::unique_ptr<Source> {
            return std::make_unique<SystemPowerSource>(config, parent);
        });
    Discovery::registerFactory(
        SourceType::System, [](const QVariantMap&, const QVariantMap&, QObject* parent) {
            return static_cast<Discovery*>(new SystemDiscovery(parent));
        });
}

}  // namespace nutty
