// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/sources/Sources.h"

#include "core/model/Source.h"
#include "core/sources/apcupsd/ApcupsdSource.h"
#include "core/sources/ecoflow/EcoFlowSource.h"
#include "core/sources/http/HttpJsonSource.h"
#include "core/sources/nut/NutSource.h"

namespace nutty {

namespace {

template <typename T> SourceRegistry::Factory factoryFor()
{
    return [](const DeviceConfig& config, QObject* parent) -> std::unique_ptr<Source> {
        return std::make_unique<T>(config, parent);
    };
}

template <typename T> Discovery::Factory discoveryFor()
{
    return [](const QVariantMap& settings, const QVariantMap& secrets, QObject* parent) -> Discovery* {
        return new T(settings, secrets, parent);
    };
}

}  // namespace

void registerCoreSources()
{
    SourceRegistry::registerFactory(SourceType::Nut, factoryFor<NutSource>());
    SourceRegistry::registerFactory(SourceType::Apcupsd, factoryFor<ApcupsdSource>());
    SourceRegistry::registerFactory(SourceType::EcoFlow, factoryFor<EcoFlowSource>());
    SourceRegistry::registerFactory(SourceType::HttpJson, factoryFor<HttpJsonSource>());

    Discovery::registerFactory(SourceType::Nut, discoveryFor<NutDiscovery>());
    Discovery::registerFactory(SourceType::EcoFlow, discoveryFor<EcoFlowDiscovery>());
}

}  // namespace nutty
