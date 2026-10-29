// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/storage/ConfigStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace nutty {

namespace {

QString uuidKey(const QUuid& id)
{
    return id.toString(QUuid::WithoutBraces);
}

QStringList uuidList(const QList<QUuid>& ids)
{
    QStringList out;
    out.reserve(ids.size());
    for (const auto& id : ids)
        out << id.toString();
    return out;
}

QList<QUuid> parseUuidList(const QStringList& list)
{
    QList<QUuid> out;
    for (const auto& s : list) {
        const QUuid id(s);
        if (!id.isNull())
            out << id;
    }
    return out;
}

QUuid readId(const QSettings& s)
{
    QUuid id(s.value(QStringLiteral("id")).toString());
    return id.isNull() ? QUuid::createUuid() : id;
}

// Nested key/value maps (DeviceConfig::settings, ChannelConfig::settings).
void writeMap(QSettings& s, const QString& group, const QVariantMap& map, const QStringList& skip)
{
    s.beginGroup(group);
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        if (!skip.contains(it.key()))
            s.setValue(it.key(), it.value());
    }
    s.endGroup();
}

QVariantMap readMap(QSettings& s, const QString& group)
{
    QVariantMap map;
    s.beginGroup(group);
    for (const auto& key : s.childKeys())
        map.insert(key, s.value(key));
    s.endGroup();
    return map;
}

// The union of every type's secret keys, used to clean up after a device or
// channel whose type is no longer known.
QStringList allDeviceSecretKeys()
{
    QStringList keys;
    for (auto type : { SourceType::Nut, SourceType::Apcupsd, SourceType::EcoFlow, SourceType::HttpJson,
             SourceType::System })
        keys << secretKeys(type);
    keys.removeDuplicates();
    return keys;
}

QStringList allChannelSecretKeys()
{
    QStringList keys;
    for (auto type : { ChannelType::Desktop, ChannelType::Pushover, ChannelType::Ntfy, ChannelType::Email,
             ChannelType::Webhook, ChannelType::PagerDuty })
        keys << secretKeys(type);
    keys.removeDuplicates();
    return keys;
}

}  // namespace

ConfigStore::ConfigStore(QString iniPath, SecretStore* secrets)
    : m_path(std::move(iniPath))
    , m_secrets(secrets)
{
}

bool ConfigStore::exists() const
{
    return QFileInfo::exists(m_path);
}

QString ConfigStore::deviceSecretKey(const QUuid& device, const QString& key)
{
    return QStringLiteral("device/%1/%2").arg(uuidKey(device), key);
}

QString ConfigStore::channelSecretKey(const QUuid& channel, const QString& key)
{
    return QStringLiteral("channel/%1/%2").arg(uuidKey(channel), key);
}

AppConfig ConfigStore::defaults()
{
    AppConfig config;
    ChannelConfig desktop;
    desktop.id = QUuid::createUuid();
    desktop.name = QCoreApplication::translate("nutty::Storage", "Desktop notifications");
    desktop.type = ChannelType::Desktop;
    desktop.events = defaultChannelEvents(ChannelType::Desktop);
    config.channels << desktop;
    return config;
}

void ConfigStore::rememberIds(const AppConfig& config)
{
    m_knownDevices.clear();
    for (const auto& d : config.devices)
        m_knownDevices.insert(d.id, d.type);
    m_knownChannels.clear();
    for (const auto& c : config.channels)
        m_knownChannels.insert(c.id, c.type);
}

void ConfigStore::addError(const QString& error)
{
    if (!m_lastError.isEmpty())
        m_lastError += u'\n';
    m_lastError += error;
}

void ConfigStore::migrate(QSettings& settings, int fromVersion)
{
    // Schema 1 is the first release; future steps go here, e.g.
    //   if (fromVersion < 2) { ...rename keys...; }
    Q_UNUSED(fromVersion)
    settings.setValue(QStringLiteral("schemaVersion"), AppConfig::kSchemaVersion);
}

AppConfig ConfigStore::load()
{
    m_lastError.clear();
    if (!exists()) {
        auto config = defaults();
        rememberIds(config);
        m_secretCache.clear();
        return config;
    }

    QSettings s(m_path, QSettings::IniFormat);
    if (s.status() != QSettings::NoError)
        addError(QCoreApplication::translate("nutty::Storage", "Could not read %1").arg(m_path));

    const int version = s.value(QStringLiteral("schemaVersion"), 0).toInt();
    if (version < AppConfig::kSchemaVersion)
        migrate(s, version);

    AppConfig config;
    config.schemaVersion = AppConfig::kSchemaVersion;

    // General -----------------------------------------------------------------
    s.beginGroup(QStringLiteral("general"));
    auto& g = config.general;
    g.theme = enumFromKey(s.value(QStringLiteral("theme")).toString(), g.theme);
    g.motion = enumFromKey(s.value(QStringLiteral("motion")).toString(), g.motion);
    g.translucentWindow = s.value(QStringLiteral("translucentWindow"), g.translucentWindow).toBool();
    g.useSystemTitleBar = s.value(QStringLiteral("useSystemTitleBar"), g.useSystemTitleBar).toBool();
    g.autostart = s.value(QStringLiteral("autostart"), g.autostart).toBool();
    g.keepWindowLoaded = s.value(QStringLiteral("keepWindowLoaded"), g.keepWindowLoaded).toBool();
    g.trayIcon = enumFromKey(s.value(QStringLiteral("trayIcon")).toString(), g.trayIcon);
    g.monochromeTrayIcon = s.value(QStringLiteral("monochromeTrayIcon"), g.monochromeTrayIcon).toBool();
    g.language = s.value(QStringLiteral("language"), g.language).toString();
    g.sampleRetentionDays = s.value(QStringLiteral("sampleRetentionDays"), g.sampleRetentionDays).toInt();
    g.eventRetentionDays = s.value(QStringLiteral("eventRetentionDays"), g.eventRetentionDays).toInt();
    s.endGroup();

    // Actions -----------------------------------------------------------------
    s.beginGroup(QStringLiteral("actions"));
    auto& a = config.actions;
    a.safetyMarginSec = s.value(QStringLiteral("safetyMarginSec"), a.safetyMarginSec).toInt();
    a.stableOnlineSec = s.value(QStringLiteral("stableOnlineSec"), a.stableOnlineSec).toInt();
    a.deadTimeSec = s.value(QStringLiteral("deadTimeSec"), a.deadTimeSec).toInt();
    a.wakeGraceSec = s.value(QStringLiteral("wakeGraceSec"), a.wakeGraceSec).toInt();
    a.fsdDelaySec = s.value(QStringLiteral("fsdDelaySec"), a.fsdDelaySec).toInt();
    s.endGroup();

    // Devices -----------------------------------------------------------------
    const int deviceCount = s.beginReadArray(QStringLiteral("devices"));
    for (int i = 0; i < deviceCount; ++i) {
        s.setArrayIndex(i);
        DeviceConfig d;
        d.id = readId(s);
        d.name = s.value(QStringLiteral("name")).toString();
        d.type = enumFromKey(s.value(QStringLiteral("type")).toString(), SourceType::Nut);
        d.enabled = s.value(QStringLiteral("enabled"), true).toBool();
        d.pollIntervalSec = s.value(QStringLiteral("pollIntervalSec"), 0).toInt();
        d.kind = enumFromKey(s.value(QStringLiteral("kind")).toString(), DeviceKind::Auto);
        d.customImage = s.value(QStringLiteral("customImage")).toString();
        d.channels = parseUuidList(s.value(QStringLiteral("channels")).toStringList());
        d.notifyChargeBelow = s.value(QStringLiteral("notifyChargeBelow"), d.notifyChargeBelow).toInt();
        d.notifyRuntimeBelowSec
            = s.value(QStringLiteral("notifyRuntimeBelowSec"), d.notifyRuntimeBelowSec).toInt();
        d.commLostCritical
            = s.value(QStringLiteral("commLostCritical"), defaultCommLostCritical(d.type)).toBool();
        d.settings = readMap(s, QStringLiteral("settings"));
        // A secret that somehow ended up in the INI moves to the secrets map
        // (and out of the file on the next save).
        for (const auto& key : secretKeys(d.type)) {
            if (d.settings.contains(key))
                d.secrets.insert(key, d.settings.take(key));
        }
        config.devices << d;
    }
    s.endArray();

    // Groups ------------------------------------------------------------------
    const int groupCount = s.beginReadArray(QStringLiteral("groups"));
    for (int i = 0; i < groupCount; ++i) {
        s.setArrayIndex(i);
        DeviceGroup group;
        group.id = readId(s);
        group.name = s.value(QStringLiteral("name")).toString();
        group.members = parseUuidList(s.value(QStringLiteral("members")).toStringList());
        config.groups << group;
    }
    s.endArray();

    // Rules -------------------------------------------------------------------
    const int ruleCount = s.beginReadArray(QStringLiteral("rules"));
    for (int i = 0; i < ruleCount; ++i) {
        s.setArrayIndex(i);
        Rule r;
        r.id = readId(s);
        r.name = s.value(QStringLiteral("name")).toString();
        r.enabled = s.value(QStringLiteral("enabled"), true).toBool();
        r.scope = enumFromKey(s.value(QStringLiteral("scope")).toString(), r.scope);
        r.target = QUuid(s.value(QStringLiteral("target")).toString());
        r.condition = enumFromKey(s.value(QStringLiteral("condition")).toString(), r.condition);
        r.threshold = s.value(QStringLiteral("threshold"), r.threshold).toDouble();
        r.action = enumFromKey(s.value(QStringLiteral("action")).toString(), r.action);
        r.countdownSec = s.value(QStringLiteral("countdownSec"), r.countdownSec).toInt();
        r.command = s.value(QStringLiteral("command")).toString();
        config.rules << r;
    }
    s.endArray();

    // Channels ----------------------------------------------------------------
    const int channelCount = s.beginReadArray(QStringLiteral("channels"));
    for (int i = 0; i < channelCount; ++i) {
        s.setArrayIndex(i);
        ChannelConfig c;
        c.id = readId(s);
        c.name = s.value(QStringLiteral("name")).toString();
        c.type = enumFromKey(s.value(QStringLiteral("type")).toString(), ChannelType::Desktop);
        c.enabled = s.value(QStringLiteral("enabled"), true).toBool();
        if (s.contains(QStringLiteral("events"))) {
            for (const auto& key : s.value(QStringLiteral("events")).toStringList()) {
                const auto type = enumFromKey(key, EventType::Test);
                if (type != EventType::Test || key == enumKey(EventType::Test))
                    c.events.insert(type);
            }
        } else {
            c.events = defaultChannelEvents(c.type);
        }
        c.settings = readMap(s, QStringLiteral("settings"));
        for (const auto& key : secretKeys(c.type)) {
            if (c.settings.contains(key))
                c.secrets.insert(key, c.settings.take(key));
        }
        config.channels << c;
    }
    s.endArray();

    loadSecrets(config);

    rememberIds(config);
    return config;
}

bool ConfigStore::save(const AppConfig& config)
{
    m_lastError.clear();
    QDir().mkpath(QFileInfo(m_path).absolutePath());

    QSettings s(m_path, QSettings::IniFormat);
    s.clear();
    s.setValue(QStringLiteral("schemaVersion"), AppConfig::kSchemaVersion);

    s.beginGroup(QStringLiteral("general"));
    const auto& g = config.general;
    s.setValue(QStringLiteral("theme"), enumKey(g.theme));
    s.setValue(QStringLiteral("motion"), enumKey(g.motion));
    s.setValue(QStringLiteral("translucentWindow"), g.translucentWindow);
    s.setValue(QStringLiteral("useSystemTitleBar"), g.useSystemTitleBar);
    s.setValue(QStringLiteral("autostart"), g.autostart);
    s.setValue(QStringLiteral("keepWindowLoaded"), g.keepWindowLoaded);
    s.setValue(QStringLiteral("trayIcon"), enumKey(g.trayIcon));
    s.setValue(QStringLiteral("monochromeTrayIcon"), g.monochromeTrayIcon);
    s.setValue(QStringLiteral("language"), g.language);
    s.setValue(QStringLiteral("sampleRetentionDays"), g.sampleRetentionDays);
    s.setValue(QStringLiteral("eventRetentionDays"), g.eventRetentionDays);
    s.endGroup();

    s.beginGroup(QStringLiteral("actions"));
    const auto& a = config.actions;
    s.setValue(QStringLiteral("safetyMarginSec"), a.safetyMarginSec);
    s.setValue(QStringLiteral("stableOnlineSec"), a.stableOnlineSec);
    s.setValue(QStringLiteral("deadTimeSec"), a.deadTimeSec);
    s.setValue(QStringLiteral("wakeGraceSec"), a.wakeGraceSec);
    s.setValue(QStringLiteral("fsdDelaySec"), a.fsdDelaySec);
    s.endGroup();

    s.beginWriteArray(QStringLiteral("devices"), static_cast<int>(config.devices.size()));
    for (int i = 0; i < config.devices.size(); ++i) {
        const auto& d = config.devices.at(i);
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("id"), d.id.toString());
        s.setValue(QStringLiteral("name"), d.name);
        s.setValue(QStringLiteral("type"), enumKey(d.type));
        s.setValue(QStringLiteral("enabled"), d.enabled);
        s.setValue(QStringLiteral("pollIntervalSec"), d.pollIntervalSec);
        s.setValue(QStringLiteral("kind"), enumKey(d.kind));
        s.setValue(QStringLiteral("customImage"), d.customImage);
        s.setValue(QStringLiteral("channels"), uuidList(d.channels));
        s.setValue(QStringLiteral("notifyChargeBelow"), d.notifyChargeBelow);
        s.setValue(QStringLiteral("notifyRuntimeBelowSec"), d.notifyRuntimeBelowSec);
        s.setValue(QStringLiteral("commLostCritical"), d.commLostCritical);
        writeMap(s, QStringLiteral("settings"), d.settings, allDeviceSecretKeys());
    }
    s.endArray();

    s.beginWriteArray(QStringLiteral("groups"), static_cast<int>(config.groups.size()));
    for (int i = 0; i < config.groups.size(); ++i) {
        const auto& group = config.groups.at(i);
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("id"), group.id.toString());
        s.setValue(QStringLiteral("name"), group.name);
        s.setValue(QStringLiteral("members"), uuidList(group.members));
    }
    s.endArray();

    s.beginWriteArray(QStringLiteral("rules"), static_cast<int>(config.rules.size()));
    for (int i = 0; i < config.rules.size(); ++i) {
        const auto& r = config.rules.at(i);
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("id"), r.id.toString());
        s.setValue(QStringLiteral("name"), r.name);
        s.setValue(QStringLiteral("enabled"), r.enabled);
        s.setValue(QStringLiteral("scope"), enumKey(r.scope));
        s.setValue(QStringLiteral("target"), r.target.toString());
        s.setValue(QStringLiteral("condition"), enumKey(r.condition));
        s.setValue(QStringLiteral("threshold"), r.threshold);
        s.setValue(QStringLiteral("action"), enumKey(r.action));
        s.setValue(QStringLiteral("countdownSec"), r.countdownSec);
        s.setValue(QStringLiteral("command"), r.command);
    }
    s.endArray();

    s.beginWriteArray(QStringLiteral("channels"), static_cast<int>(config.channels.size()));
    for (int i = 0; i < config.channels.size(); ++i) {
        const auto& c = config.channels.at(i);
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("id"), c.id.toString());
        s.setValue(QStringLiteral("name"), c.name);
        s.setValue(QStringLiteral("type"), enumKey(c.type));
        s.setValue(QStringLiteral("enabled"), c.enabled);
        QStringList events;
        for (const auto type : c.events)
            events << enumKey(type);
        events.sort();
        s.setValue(QStringLiteral("events"), events);
        writeMap(s, QStringLiteral("settings"), c.settings, allChannelSecretKeys());
    }
    s.endArray();

    s.sync();
    const bool ok = s.status() == QSettings::NoError;
    if (!ok)
        addError(QCoreApplication::translate("nutty::Storage", "Could not write %1").arg(m_path));

    saveSecrets(config);
    rememberIds(config);
    return ok;
}

void ConfigStore::loadSecrets(AppConfig& config)
{
    m_secretCache.clear();
    if (!m_secrets)
        return;
    const auto fill = [this](QVariantMap& secrets, const QStringList& keys, auto keyFor) {
        for (const auto& key : keys) {
            if (secrets.contains(key))
                continue;  // migrated out of the INI above; stored on the next save
            const auto full = keyFor(key);
            auto value = m_secrets->read(full);
            if (!value) {
                addError(value.error());
                continue;
            }
            if (*value) {
                secrets.insert(key, **value);
                m_secretCache.insert(full, **value);
            }
        }
    };
    for (auto& d : config.devices)
        fill(d.secrets, secretKeys(d.type), [&](const QString& k) { return deviceSecretKey(d.id, k); });
    for (auto& c : config.channels)
        fill(c.secrets, secretKeys(c.type), [&](const QString& k) { return channelSecretKey(c.id, k); });
}

void ConfigStore::saveSecrets(const AppConfig& config)
{
    if (!m_secrets)
        return;

    const auto put = [this](const QString& full, const QString& value) {
        if (value.isEmpty()) {
            if (m_secretCache.remove(full) == 0)
                return;
            if (auto r = m_secrets->remove(full); !r)
                addError(r.error());
        } else {
            if (m_secretCache.value(full) == value && m_secretCache.contains(full))
                return;
            if (auto r = m_secrets->write(full, value); !r) {
                addError(r.error());
                return;
            }
            m_secretCache.insert(full, value);
        }
    };
    const auto drop = [this](const QString& full) {
        m_secretCache.remove(full);
        if (auto r = m_secrets->remove(full); !r)
            addError(r.error());
    };

    QHash<QUuid, SourceType> devices;
    for (const auto& d : config.devices) {
        devices.insert(d.id, d.type);
        const auto keys = secretKeys(d.type);
        for (const auto& key : keys)
            put(deviceSecretKey(d.id, key), d.secrets.value(key).toString());
        // The type changed: the old type's secrets are no longer used.
        if (m_knownDevices.contains(d.id) && m_knownDevices.value(d.id) != d.type) {
            for (const auto& key : secretKeys(m_knownDevices.value(d.id))) {
                if (!keys.contains(key))
                    drop(deviceSecretKey(d.id, key));
            }
        }
    }
    for (auto it = m_knownDevices.cbegin(); it != m_knownDevices.cend(); ++it) {
        if (devices.contains(it.key()))
            continue;
        for (const auto& key : secretKeys(it.value()))
            drop(deviceSecretKey(it.key(), key));
    }

    QHash<QUuid, ChannelType> channels;
    for (const auto& c : config.channels) {
        channels.insert(c.id, c.type);
        const auto keys = secretKeys(c.type);
        for (const auto& key : keys)
            put(channelSecretKey(c.id, key), c.secrets.value(key).toString());
        if (m_knownChannels.contains(c.id) && m_knownChannels.value(c.id) != c.type) {
            for (const auto& key : secretKeys(m_knownChannels.value(c.id))) {
                if (!keys.contains(key))
                    drop(channelSecretKey(c.id, key));
            }
        }
    }
    for (auto it = m_knownChannels.cbegin(); it != m_knownChannels.cend(); ++it) {
        if (channels.contains(it.key()))
            continue;
        for (const auto& key : secretKeys(it.value()))
            drop(channelSecretKey(it.key(), key));
    }
}

}  // namespace nutty
