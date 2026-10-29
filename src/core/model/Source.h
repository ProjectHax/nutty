// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Config.h"
#include "core/model/DeviceControl.h"
#include "core/model/DeviceDetails.h"
#include "core/model/DeviceStatus.h"
#include "core/model/Types.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>

namespace nutty {

// A live connection to one monitored device. Implementations are async and
// single-threaded: they run on the main event loop and report through signals.
//
// Lifecycle: construct -> start() -> statusUpdated()/errorOccurred() ... -> stop().
// reset() drops the connection and cached data (used after resume from sleep)
// and starts again.
class Source : public QObject
{
    Q_OBJECT

public:
    explicit Source(DeviceConfig config, QObject* parent = nullptr);
    ~Source() override;

    const DeviceConfig& config() const { return m_config; }
    QUuid deviceId() const { return m_config.id; }

    virtual void start() = 0;
    virtual void stop() = 0;
    // Poll immediately instead of waiting for the next tick.
    virtual void pollNow() = 0;
    virtual void reset();
    // Called once the OS has accepted a shutdown that nutty requested. A NUT
    // secondary sends LOGOUT here so the primary stops waiting for it.
    virtual void prepareForShutdown() { }

    // A short human-readable description of the endpoint, e.g. "ups@nas:3493".
    virtual QString describe() const = 0;

    // Readings beyond the status, for the device page (EcoFlow: power by
    // port, battery packs, lifetime energy). Empty by default.
    virtual DeviceDetails details() const { return {}; }
    // The settings on the device that nutty can change, with their current
    // values. Empty by default.
    virtual QList<DeviceControl> controls() const { return {}; }
    // Sends a change. Returns false, with the reason in `error`, when it
    // can't be sent; otherwise controlFinished() reports how it went.
    virtual bool applyControl(const QString& id, double value, QString* error = nullptr);

    ConnectionState connectionState() const { return m_state; }
    const DeviceStatus& lastStatus() const { return m_lastStatus; }
    SourceError lastErrorKind() const { return m_lastErrorKind; }
    QString lastError() const { return m_lastError; }
    int pollIntervalMs() const { return m_config.effectivePollIntervalSec() * 1000; }

signals:
    void statusUpdated(const nutty::DeviceStatus& status);
    void connectionStateChanged(nutty::ConnectionState state);
    void errorOccurred(nutty::SourceError kind, const QString& message);
    // controls() changed: new readings, or a change on its way or done.
    void controlsChanged();
    void controlFinished(const QString& id, double value, nutty::ControlResult result, const QString& error);

protected:
    // Stamps sampledAt (if unset), stores the status, sets state Connected,
    // clears the last error and emits statusUpdated.
    void publish(DeviceStatus status);
    void setConnectionState(ConnectionState state);
    // Records the error, sets state Error and emits errorOccurred.
    void reportError(SourceError kind, const QString& message);
    void clearLastStatus() { m_lastStatus = {}; }

private:
    DeviceConfig m_config;
    DeviceStatus m_lastStatus;
    ConnectionState m_state = ConnectionState::Disconnected;
    SourceError m_lastErrorKind = SourceError::None;
    QString m_lastError;
};

// Creates sources by type. nutty_sources registers NUT/apcupsd/EcoFlow/HTTP,
// nutty_platform registers System; tests register fakes. Keeps the engine free
// of link dependencies on the implementations.
class SourceRegistry
{
public:
    using Factory = std::function<std::unique_ptr<Source>(const DeviceConfig&, QObject* parent)>;

    static void registerFactory(SourceType type, Factory factory);
    static bool hasFactory(SourceType type);
    // nullptr if no factory is registered for config.type.
    static std::unique_ptr<Source> create(const DeviceConfig& config, QObject* parent = nullptr);
};

// One entry found by Discovery (a UPS on a NUT server, an EcoFlow serial, an
// OS power source, ...).
struct DiscoveredDevice {
    QString id;  // value to store in the source's id setting (UPS name, serial, source id)
    QString name;  // display name
    QString description;  // e.g. model string
    bool online = true;
};

// Async one-shot lookup of the devices an endpoint offers, for the "Add device"
// pickers. Emits exactly one of finished()/failed(), then deletes itself.
class Discovery : public QObject
{
    Q_OBJECT

public:
    using Factory
        = std::function<Discovery*(const QVariantMap& settings, const QVariantMap& secrets, QObject* parent)>;

    static void registerFactory(SourceType type, Factory factory);
    static bool supports(SourceType type);
    // Starts a discovery; nullptr if the type has no discovery.
    static Discovery* start(
        SourceType type, const QVariantMap& settings, const QVariantMap& secrets, QObject* parent = nullptr);

signals:
    void finished(const QList<nutty::DiscoveredDevice>& devices);
    void failed(const QString& error);

protected:
    using QObject::QObject;
    void finish(const QList<DiscoveredDevice>& devices);
    void fail(const QString& error);
};

}  // namespace nutty
