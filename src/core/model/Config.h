// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Types.h"

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVariantMap>

// The persisted configuration. ConfigStore (core/storage) loads and saves it;
// secrets never touch the INI file: ConfigStore moves every key listed by
// secretKeys() into the SecretStore and fills the in-memory `secrets` maps.
namespace nutty {

// ---------------------------------------------------------------------------
// Per-source settings keys (DeviceConfig::settings / ::secrets).
// ---------------------------------------------------------------------------
namespace keys {
// NUT
inline constexpr auto NutHost = "host";  // default "localhost"
inline constexpr auto NutPort = "port";  // default 3493
inline constexpr auto NutUps = "ups";  // UPS name on the server
inline constexpr auto NutUsername = "username";  // optional
inline constexpr auto NutTls = "tls";  // "off" | "required"
inline constexpr auto NutFingerprint = "fingerprint";  // pinned SHA-256 (hex) for self-signed certs
inline constexpr auto NutSecondary = "secondary";  // bool: LOGIN as an upsmon secondary
inline constexpr auto NutPassword = "password";  // secret

// apcupsd
inline constexpr auto ApcHost = "host";  // default "localhost"
inline constexpr auto ApcPort = "port";  // default 3551

// EcoFlow (IoT Open Platform)
inline constexpr auto EcoRegion = "region";  // "global" | "eu" | "custom"
inline constexpr auto EcoHost = "host";  // used when region == "custom"
inline constexpr auto EcoSerial = "serial";  // device serial number
inline constexpr auto EcoAccessKey = "accessKey";  // secret
inline constexpr auto EcoSecretKey = "secretKey";  // secret

// Generic HTTP/JSON
inline constexpr auto HttpPreset = "preset";  // "generic" | "homeassistant"
inline constexpr auto HttpUrl = "url";  // generic: full URL; HA: base URL
inline constexpr auto HttpHeaders = "headers";  // extra "Name: value" lines, newline separated
inline constexpr auto HttpInsecureTls = "insecureTls";  // bool: accept self-signed certs
inline constexpr auto HttpToken = "token";  // secret: bearer token (HA long-lived token)
// Field mapping. Generic preset: dotted JSON paths into the response
// ("battery.soc", "data[0].value"). Home Assistant preset: entity ids
// ("sensor.ups_battery_charge"), each fetched from /api/states/<entity>.
inline constexpr auto HttpCharge = "charge";
inline constexpr auto HttpRuntime = "runtime";
inline constexpr auto HttpRuntimeUnit = "runtimeUnit";  // "s" | "min" | "h"
inline constexpr auto HttpLoad = "load";
inline constexpr auto HttpWattsIn = "wattsIn";
inline constexpr auto HttpWattsOut = "wattsOut";
inline constexpr auto HttpInputVoltage = "inputVoltage";
inline constexpr auto HttpOutputVoltage = "outputVoltage";
inline constexpr auto HttpTemperature = "temperature";
inline constexpr auto HttpStatus = "status";  // path/entity holding a state value
inline constexpr auto HttpOnBatteryValues
    = "onBatteryValues";  // comma list, e.g. "discharging,on battery,OB"
inline constexpr auto HttpChargingValues = "chargingValues";  // comma list
inline constexpr auto HttpLowBatteryValues = "lowBatteryValues";
inline constexpr auto HttpOnlineValues = "onlineValues";

// OS power sources
inline constexpr auto SystemSourceId = "sourceId";  // empty = first UPS, else first battery

// Channels ------------------------------------------------------------------
inline constexpr auto PushoverUserKey = "userKey";  // secret
inline constexpr auto PushoverApiToken = "apiToken";  // secret
inline constexpr auto PushoverDevice = "device";  // optional device name
inline constexpr auto PushoverSound = "sound";  // optional

inline constexpr auto NtfyServer = "server";  // default "https://ntfy.sh"
inline constexpr auto NtfyTopic = "topic";
inline constexpr auto NtfyUsername = "username";  // optional, basic auth
inline constexpr auto NtfyPassword = "password";  // secret, basic auth
inline constexpr auto NtfyToken = "token";  // secret, bearer (access token)

inline constexpr auto EmailHost = "host";
inline constexpr auto EmailPort = "port";  // default by security: 25 / 587 / 465
inline constexpr auto EmailSecurity = "security";  // "none" | "starttls" | "tls"
inline constexpr auto EmailUsername = "username";
inline constexpr auto EmailPassword = "password";  // secret
inline constexpr auto EmailFrom = "from";
inline constexpr auto EmailTo = "to";  // comma-separated

inline constexpr auto WebhookUrl = "url";
inline constexpr auto WebhookMethod = "method";  // "POST" | "PUT"
inline constexpr auto WebhookPreset = "preset";  // "json" | "discord" | "slack" | "teams" | "custom"
inline constexpr auto WebhookTemplate = "template";  // body for "custom", with {placeholders}
inline constexpr auto WebhookHeaders = "headers";  // extra "Name: value" lines
inline constexpr auto WebhookAuthHeader = "authHeader";  // header name for the secret, e.g. "Authorization"
inline constexpr auto WebhookAuthValue = "authValue";  // secret

inline constexpr auto PagerDutyRoutingKey = "routingKey";  // secret: integration key (Events API v2)
inline constexpr auto PagerDutyRegion = "region";  // "us" | "eu"
}  // namespace keys

// Settings keys that hold secrets for each type.
QStringList secretKeys(SourceType type);
QStringList secretKeys(ChannelType type);

// ---------------------------------------------------------------------------

struct DeviceConfig {
    QUuid id;
    QString name;
    SourceType type = SourceType::Nut;
    bool enabled = true;
    int pollIntervalSec = 0;  // 0 = defaultPollIntervalSec(type)
    DeviceKind kind = DeviceKind::Auto;
    QString customImage;  // file name under AppDataLocation/images, empty = illustration
    QVariantMap settings;  // keys:: above
    QVariantMap secrets;  // in memory only, never written to the INI
    QList<QUuid> channels;  // notification channels for this device; empty = all
    int notifyChargeBelow = 50;  // percent; 0 = off. Raises ChargeBelow once per episode
    int notifyRuntimeBelowSec = 300;  // seconds; 0 = off. Raises RuntimeBelow once per episode
    // Treat "comms lost while on battery" as critical (upsmon NOCOMM). Off by
    // default for cloud/HTTP sources, whose network often dies with the power.
    bool commLostCritical = true;

    int effectivePollIntervalSec() const;
};

int defaultPollIntervalSec(SourceType type);
bool defaultCommLostCritical(SourceType type);

struct DeviceGroup {
    QUuid id;
    QString name;
    QList<QUuid> members;
};

struct Rule {
    QUuid id;
    QString name;
    bool enabled = true;
    RuleScope scope = RuleScope::Device;
    QUuid target;  // device id or group id, depending on scope
    ConditionType condition = ConditionType::ChargeBelow;
    double threshold = 20;  // unit depends on condition (see ConditionType)
    ActionType action = ActionType::Shutdown;
    int countdownSec = 60;  // ignored for FSD, which acts immediately
    QString command;  // for ActionType::Command
};

struct ChannelConfig {
    QUuid id;
    QString name;
    ChannelType type = ChannelType::Desktop;
    bool enabled = true;
    QSet<EventType> events;  // which events go to this channel
    QVariantMap settings;  // keys:: above
    QVariantMap secrets;  // in memory only
};

// The events a new channel subscribes to.
QSet<EventType> defaultChannelEvents(ChannelType type);

struct GeneralSettings {
    ThemeMode theme = ThemeMode::System;
    MotionMode motion = MotionMode::System;
    bool translucentWindow = true;
    bool useSystemTitleBar = false;  // Linux: off = nutty's own title bar
    bool autostart = false;
    bool keepWindowLoaded = false;
    TrayIconStyle trayIcon = TrayIconStyle::Acorn;
    bool monochromeTrayIcon = false;
    QString language;  // empty = follow the system
    int sampleRetentionDays = 30;
    int eventRetentionDays = 365;
};

struct ActionSettings {
    int safetyMarginSec = 30;  // countdown never runs past (runtime - margin)
    int stableOnlineSec = 30;  // line power must hold this long to end an episode
    int deadTimeSec = 15;  // no data for this long = comms lost
    int wakeGraceSec = 30;  // after resume, wait this long (and for fresh data) before acting
    int fsdDelaySec = 5;  // FSD acts after this, with no countdown
};

struct AppConfig {
    static constexpr int kSchemaVersion = 1;

    int schemaVersion = kSchemaVersion;
    GeneralSettings general;
    ActionSettings actions;
    QList<DeviceConfig> devices;
    QList<DeviceGroup> groups;
    QList<Rule> rules;
    QList<ChannelConfig> channels;

    const DeviceConfig* device(const QUuid& id) const;
    const DeviceGroup* group(const QUuid& id) const;
    const ChannelConfig* channel(const QUuid& id) const;
};

}  // namespace nutty
