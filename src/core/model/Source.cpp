// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/model/Source.h"

#include <QHash>

namespace nutty {

Source::Source(DeviceConfig config, QObject* parent)
    : QObject(parent)
    , m_config(std::move(config))
{
}

Source::~Source() = default;

void Source::reset()
{
    stop();
    clearLastStatus();
    start();
}

bool Source::applyControl(const QString& id, double value, QString* error)
{
    Q_UNUSED(id);
    Q_UNUSED(value);
    if (error)
        *error = tr("This device has no settings nutty can change.");
    return false;
}

void Source::publish(DeviceStatus status)
{
    if (!status.sampledAt.isValid())
        status.sampledAt = QDateTime::currentDateTimeUtc();
    m_lastStatus = std::move(status);
    m_lastErrorKind = SourceError::None;
    m_lastError.clear();
    setConnectionState(ConnectionState::Connected);
    emit statusUpdated(m_lastStatus);
}

void Source::setConnectionState(ConnectionState state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit connectionStateChanged(state);
}

void Source::reportError(SourceError kind, const QString& message)
{
    m_lastErrorKind = kind;
    m_lastError = message;
    setConnectionState(ConnectionState::Error);
    emit errorOccurred(kind, message);
}

// --- SourceRegistry ---------------------------------------------------------

namespace {

QHash<SourceType, SourceRegistry::Factory>& sourceFactories()
{
    static QHash<SourceType, SourceRegistry::Factory> factories;
    return factories;
}

QHash<SourceType, Discovery::Factory>& discoveryFactories()
{
    static QHash<SourceType, Discovery::Factory> factories;
    return factories;
}

}  // namespace

void SourceRegistry::registerFactory(SourceType type, Factory factory)
{
    sourceFactories().insert(type, std::move(factory));
}

bool SourceRegistry::hasFactory(SourceType type)
{
    return sourceFactories().contains(type);
}

std::unique_ptr<Source> SourceRegistry::create(const DeviceConfig& config, QObject* parent)
{
    const auto it = sourceFactories().constFind(config.type);
    if (it == sourceFactories().cend())
        return nullptr;
    return (*it)(config, parent);
}

// --- Discovery ---------------------------------------------------------------

void Discovery::registerFactory(SourceType type, Factory factory)
{
    discoveryFactories().insert(type, std::move(factory));
}

bool Discovery::supports(SourceType type)
{
    return discoveryFactories().contains(type);
}

Discovery* Discovery::start(
    SourceType type, const QVariantMap& settings, const QVariantMap& secrets, QObject* parent)
{
    const auto it = discoveryFactories().constFind(type);
    if (it == discoveryFactories().cend())
        return nullptr;
    return (*it)(settings, secrets, parent);
}

void Discovery::finish(const QList<DiscoveredDevice>& devices)
{
    emit finished(devices);
    deleteLater();
}

void Discovery::fail(const QString& error)
{
    emit failed(error);
    deleteLater();
}

}  // namespace nutty
