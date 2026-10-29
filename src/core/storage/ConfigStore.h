// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Config.h"
#include "core/model/SecretStore.h"

#include <QHash>
#include <QString>
#include <QUuid>

class QSettings;

namespace nutty {

// Loads and saves the AppConfig as an INI file (QSettings). Secrets never land
// in the INI: every key listed by secretKeys() is moved to the SecretStore
// under "device/<uuid>/<key>" or "channel/<uuid>/<key>" and filled back into
// the in-memory `secrets` maps on load.
class ConfigStore
{
public:
    // secrets may be null, in which case secrets live in memory only.
    ConfigStore(QString iniPath, SecretStore* secrets);

    // Defaults when the file doesn't exist yet.
    AppConfig load();
    bool save(const AppConfig& config);

    bool exists() const;
    QString path() const { return m_path; }
    // Accumulated problems from the last load()/save() (secret store errors
    // included); empty when everything went fine.
    QString lastError() const { return m_lastError; }

    // A fresh install: a desktop notification channel and nothing else.
    static AppConfig defaults();

    static QString deviceSecretKey(const QUuid& device, const QString& key);
    static QString channelSecretKey(const QUuid& channel, const QString& key);

private:
    // Upgrades an older file in place, one schema step at a time.
    void migrate(QSettings& settings, int fromVersion);
    void loadSecrets(AppConfig& config);
    void saveSecrets(const AppConfig& config);
    void rememberIds(const AppConfig& config);
    void addError(const QString& error);

    QString m_path;
    SecretStore* m_secrets = nullptr;
    QString m_lastError;
    // Ids (and their types) as of the last load/save, so removed devices and
    // channels get their secrets deleted.
    QHash<QUuid, SourceType> m_knownDevices;
    QHash<QUuid, ChannelType> m_knownChannels;
    // What the SecretStore holds as far as we know (full key -> value), so a
    // save only touches the keychain for secrets that actually changed.
    QHash<QString, QString> m_secretCache;
};

}  // namespace nutty
