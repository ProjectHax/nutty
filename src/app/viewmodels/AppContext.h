// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Config.h"

#include <QObject>
#include <QString>
#include <QUrl>
#include <QUuid>

namespace nutty {

class ConfigStore;
class Dispatcher;
class HistoryStore;
class Monitor;
class SecretStore;

// What the view-models work on: the running Monitor, the stores and the
// dispatcher, and the one place that changes the configuration. The
// application (or a test) creates it before the first QML engine; every
// view-model picks up instance() when QML creates it.
class AppContext : public QObject
{
    Q_OBJECT

public:
    struct Services {
        Monitor* monitor = nullptr;  // required
        HistoryStore* history = nullptr;  // null: no history
        Dispatcher* dispatcher = nullptr;  // null: no notifications
        ConfigStore* configStore = nullptr;  // null: changes are applied, not saved
        SecretStore* secrets = nullptr;  // for the "secure storage" status
        QString dataDir;  // custom device images live in <dataDir>/images
        bool dryRunForced = false;  // --dry-run / NUTTY_DRY_RUN: can't be turned off
    };

    explicit AppContext(const Services& services, QObject* parent = nullptr);
    ~AppContext() override;

    // The most recently created context that is still alive.
    static AppContext* instance();

    Monitor* monitor() const { return m_services.monitor; }
    HistoryStore* history() const { return m_services.history; }
    Dispatcher* dispatcher() const { return m_services.dispatcher; }
    ConfigStore* configStore() const { return m_services.configStore; }
    SecretStore* secrets() const { return m_services.secrets; }
    bool dryRunForced() const { return m_services.dryRunForced; }

    const AppConfig& config() const { return m_config; }
    // Applies a config without saving it (startup, tests).
    void setConfig(const AppConfig& config);
    // Saves the config (when there is a ConfigStore) and applies it live.
    // On a save error it is still applied, so the session does what the user
    // asked; lastError() says what went wrong.
    bool updateConfig(const AppConfig& config);
    QString lastError() const { return m_lastError; }

    // Removes a device along with the rules about it, its group memberships,
    // its custom image and its history.
    bool removeDevice(const QUuid& id);

    QString dataDir() const { return m_services.dataDir; }
    QString imagesDir() const;
    // A file: URL for a device image, with its modification time as a query
    // so QML reloads it after a change. Empty for an empty name.
    QUrl imageUrl(const QString& fileName) const;

signals:
    // The config changed (after it was applied); `previous` is the old one.
    void configChanged(const nutty::AppConfig& previous);
    void lastErrorChanged();

private:
    void apply(const AppConfig& config);

    Services m_services;
    AppConfig m_config;
    QString m_lastError;
};

// QML talks about ids as strings without braces.
QString idString(const QUuid& id);
QUuid parseId(const QString& id);

// A path for display, with the home folder shortened to "~" (so a
// screenshot of a settings page doesn't show the user's account name), and
// the reverse.
QString displayPath(const QString& path);
QString expandPath(const QString& path);

}  // namespace nutty
