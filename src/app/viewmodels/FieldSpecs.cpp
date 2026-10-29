// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "app/viewmodels/FieldSpecs.h"

#include "core/model/Config.h"

#include <QCoreApplication>

namespace nutty::fields {

namespace {

class Text
{
    Q_DECLARE_TR_FUNCTIONS(nutty::Fields)
};

struct Field {
    QVariantMap map;

    Field(const char* key, const QString& label, const char* kind = "text")
    {
        map.insert(QStringLiteral("key"), QString::fromLatin1(key));
        map.insert(QStringLiteral("label"), label);
        map.insert(QStringLiteral("kind"), QString::fromLatin1(kind));
    }
    Field& placeholder(const QString& v)
    {
        map.insert(QStringLiteral("placeholder"), v);
        return *this;
    }
    Field& help(const QString& v)
    {
        map.insert(QStringLiteral("help"), v);
        return *this;
    }
    Field& value(const QVariant& v)
    {
        map.insert(QStringLiteral("defaultValue"), v);
        return *this;
    }
    Field& range(int lo, int hi)
    {
        map.insert(QStringLiteral("min"), lo);
        map.insert(QStringLiteral("max"), hi);
        return *this;
    }
    Field& options(const QList<QPair<QString, QString>>& list)
    {
        QVariantList out;
        for (const auto& [v, t] : list)
            out.append(QVariantMap { { QStringLiteral("value"), v }, { QStringLiteral("text"), t } });
        map.insert(QStringLiteral("options"), out);
        return *this;
    }
    Field& discover()
    {
        map.insert(QStringLiteral("discover"), true);
        return *this;
    }
    Field& advanced()
    {
        map.insert(QStringLiteral("advanced"), true);
        return *this;
    }
    Field& account()
    {
        map.insert(QStringLiteral("account"), true);
        return *this;
    }
    Field& required()
    {
        map.insert(QStringLiteral("required"), true);
        return *this;
    }
    Field& showIf(const char* key, const QVariant& v)
    {
        map.insert(QStringLiteral("showIf"), QVariantMap { { QString::fromLatin1(key), v } });
        return *this;
    }
    operator QVariant() const { return map; }  // NOLINT(google-explicit-constructor)
};

QVariantList httpMapping(bool homeAssistant)
{
    const QString hint = homeAssistant ? Text::tr("Entity id, e.g. sensor.ups_battery_charge")
                                       : Text::tr("JSON path, e.g. battery.soc or data[0].value");
    const char* preset = keys::HttpPreset;
    const QString which = homeAssistant ? QStringLiteral("homeassistant") : QStringLiteral("generic");
    auto f = [&](const char* key, const QString& label) {
        return Field(key, label).placeholder(hint).showIf(preset, which);
    };
    return {
        f(keys::HttpCharge, Text::tr("Charge (%)")),
        f(keys::HttpRuntime, Text::tr("Runtime")),
        Field(keys::HttpRuntimeUnit, Text::tr("Runtime unit"), "choice")
            .options({ { QStringLiteral("s"), Text::tr("Seconds") },
                { QStringLiteral("min"), Text::tr("Minutes") }, { QStringLiteral("h"), Text::tr("Hours") } })
            .value(homeAssistant ? QStringLiteral("min") : QStringLiteral("s"))
            .showIf(preset, which),
        f(keys::HttpStatus, Text::tr("Status")),
        f(keys::HttpLoad, Text::tr("Load (%)")).advanced(),
        f(keys::HttpWattsIn, Text::tr("Input power (W)")).advanced(),
        f(keys::HttpWattsOut, Text::tr("Output power (W)")).advanced(),
        f(keys::HttpInputVoltage, Text::tr("Input voltage")).advanced(),
        f(keys::HttpOutputVoltage, Text::tr("Output voltage")).advanced(),
        f(keys::HttpTemperature, Text::tr("Temperature (°C)")).advanced(),
    };
}

}  // namespace

QVariantList forSource(SourceType type)
{
    switch (type) {
    case SourceType::Nut:
        return {
            Field(keys::NutHost, Text::tr("Server"))
                .placeholder(QStringLiteral("localhost"))
                .value(QStringLiteral("localhost"))
                .required()
                .account(),
            Field(keys::NutPort, Text::tr("Port"), "number").range(1, 65535).value(3493).account(),
            Field(keys::NutUps, Text::tr("UPS name"))
                .placeholder(QStringLiteral("ups"))
                .discover()
                .required(),
            Field(keys::NutUsername, Text::tr("Username")).placeholder(Text::tr("Optional")).account(),
            Field(keys::NutPassword, Text::tr("Password"), "secret")
                .placeholder(Text::tr("Optional"))
                .account(),
            Field(keys::NutSecondary, Text::tr("Log in as a secondary (upsmon)"), "bool")
                .value(false)
                .help(Text::tr("Lets the NUT primary wait for this computer before it powers off. Needs a "
                               "username and password.")),
            Field(keys::NutTls, Text::tr("Encryption"), "choice")
                .options({ { QStringLiteral("off"), Text::tr("Off") },
                    { QStringLiteral("required"), Text::tr("Required (STARTTLS)") } })
                .value(QStringLiteral("off"))
                .advanced()
                .account(),
            Field(keys::NutFingerprint, Text::tr("Certificate fingerprint"))
                .placeholder(Text::tr("SHA-256, for self-signed certificates"))
                .showIf(keys::NutTls, QStringLiteral("required"))
                .advanced()
                .account(),
        };
    case SourceType::Apcupsd:
        return {
            Field(keys::ApcHost, Text::tr("Server"))
                .placeholder(QStringLiteral("localhost"))
                .value(QStringLiteral("localhost"))
                .required(),
            Field(keys::ApcPort, Text::tr("Port"), "number").range(1, 65535).value(3551),
        };
    case SourceType::EcoFlow:
        return {
            Field(keys::EcoAccessKey, Text::tr("Access key"), "secret")
                .required()
                .account()
                .help(Text::tr("From the EcoFlow developer portal (Open API).")),
            Field(keys::EcoSecretKey, Text::tr("Secret key"), "secret").required().account(),
            Field(keys::EcoSerial, Text::tr("Serial number")).discover().required(),
            Field(keys::EcoRegion, Text::tr("Region"), "choice")
                .options({ { QStringLiteral("global"), Text::tr("Global") },
                    { QStringLiteral("eu"), Text::tr("Europe") },
                    { QStringLiteral("custom"), Text::tr("Custom server") } })
                .value(QStringLiteral("global"))
                .account(),
            Field(keys::EcoHost, Text::tr("API server"))
                .placeholder(QStringLiteral("api.example.com"))
                .showIf(keys::EcoRegion, QStringLiteral("custom"))
                .account(),
        };
    case SourceType::HttpJson: {
        QVariantList list {
            Field(keys::HttpPreset, Text::tr("Kind"), "choice")
                .options({ { QStringLiteral("homeassistant"), Text::tr("Home Assistant") },
                    { QStringLiteral("generic"), Text::tr("Any JSON URL") } })
                .value(QStringLiteral("homeassistant"))
                .account(),
            Field(keys::HttpUrl, Text::tr("URL"))
                .placeholder(QStringLiteral("http://homeassistant.local:8123"))
                .required()
                .account(),
            Field(keys::HttpToken, Text::tr("Access token"), "secret")
                .account()
                .help(Text::tr("Home Assistant: a long-lived access token from your profile.")),
        };
        list.append(httpMapping(true));
        list.append(httpMapping(false));
        list.append(QVariantList {
            Field(keys::HttpOnBatteryValues, Text::tr("“On battery” values"))
                .placeholder(QStringLiteral("discharging, on battery, OB"))
                .advanced(),
            Field(keys::HttpChargingValues, Text::tr("“Charging” values"))
                .placeholder(QStringLiteral("charging"))
                .advanced(),
            Field(keys::HttpLowBatteryValues, Text::tr("“Low battery” values"))
                .placeholder(QStringLiteral("low"))
                .advanced(),
            Field(keys::HttpOnlineValues, Text::tr("“On line power” values"))
                .placeholder(QStringLiteral("online, OL"))
                .advanced(),
            Field(keys::HttpHeaders, Text::tr("Extra headers"), "multiline")
                .placeholder(QStringLiteral("Name: value"))
                .advanced()
                .account(),
            Field(keys::HttpInsecureTls, Text::tr("Accept self-signed certificates"), "bool")
                .value(false)
                .advanced()
                .account(),
        });
        return list;
    }
    case SourceType::System:
        return {
            Field(keys::SystemSourceId, Text::tr("Power source"))
                .discover()
                .placeholder(Text::tr("Automatic")),
        };
    }
    return {};
}

QVariantList forChannel(ChannelType type)
{
    switch (type) {
    case ChannelType::Desktop:
        return {};
    case ChannelType::Pushover:
        return {
            Field(keys::PushoverUserKey, Text::tr("User key"), "secret").required(),
            Field(keys::PushoverApiToken, Text::tr("API token"), "secret")
                .required()
                .help(Text::tr("Create an application at pushover.net to get one.")),
            Field(keys::PushoverDevice, Text::tr("Device")).placeholder(Text::tr("All devices")).advanced(),
            Field(keys::PushoverSound, Text::tr("Sound")).placeholder(Text::tr("Default")).advanced(),
        };
    case ChannelType::Ntfy:
        return {
            Field(keys::NtfyServer, Text::tr("Server"))
                .placeholder(QStringLiteral("https://ntfy.sh"))
                .value(QStringLiteral("https://ntfy.sh")),
            Field(keys::NtfyTopic, Text::tr("Topic"))
                .required()
                .help(Text::tr("Anyone who knows a topic on a public server can read it, so pick a "
                               "hard-to-guess name.")),
            Field(keys::NtfyToken, Text::tr("Access token"), "secret")
                .placeholder(Text::tr("Optional"))
                .advanced(),
            Field(keys::NtfyUsername, Text::tr("Username")).placeholder(Text::tr("Optional")).advanced(),
            Field(keys::NtfyPassword, Text::tr("Password"), "secret")
                .placeholder(Text::tr("Optional"))
                .advanced(),
        };
    case ChannelType::Email:
        return {
            Field(keys::EmailHost, Text::tr("SMTP server"))
                .placeholder(QStringLiteral("smtp.example.com"))
                .required(),
            Field(keys::EmailSecurity, Text::tr("Security"), "choice")
                .options({ { QStringLiteral("starttls"), Text::tr("STARTTLS") },
                    { QStringLiteral("tls"), Text::tr("TLS") },
                    { QStringLiteral("none"), Text::tr("None") } })
                .value(QStringLiteral("starttls")),
            Field(keys::EmailPort, Text::tr("Port"), "number")
                .range(0, 65535)
                .value(0)
                .help(Text::tr("0 picks the usual port for the security setting.")),
            Field(keys::EmailUsername, Text::tr("Username")),
            Field(keys::EmailPassword, Text::tr("Password"), "secret"),
            Field(keys::EmailFrom, Text::tr("From"))
                .placeholder(QStringLiteral("nutty@example.com"))
                .required(),
            Field(keys::EmailTo, Text::tr("To"))
                .placeholder(Text::tr("Comma-separated addresses"))
                .required(),
        };
    case ChannelType::Webhook:
        return {
            Field(keys::WebhookPreset, Text::tr("Format"), "choice")
                .options({ { QStringLiteral("json"), Text::tr("JSON") },
                    { QStringLiteral("discord"), Text::tr("Discord") },
                    { QStringLiteral("slack"), Text::tr("Slack") },
                    { QStringLiteral("teams"), Text::tr("Microsoft Teams") },
                    { QStringLiteral("custom"), Text::tr("Custom template") } })
                .value(QStringLiteral("json")),
            Field(keys::WebhookUrl, Text::tr("URL"))
                .placeholder(QStringLiteral("https://example.com/hook"))
                .required(),
            Field(keys::WebhookTemplate, Text::tr("Body template"), "multiline")
                .placeholder(QStringLiteral("{\"text\": \"{title}: {message}\"}"))
                .help(Text::tr("Placeholders: {title}, {message}, {device}, {type}, {severity}, {time}, "
                               "{charge}, {runtime}."))
                .showIf(keys::WebhookPreset, QStringLiteral("custom")),
            Field(keys::WebhookMethod, Text::tr("Method"), "choice")
                .options({ { QStringLiteral("POST"), QStringLiteral("POST") },
                    { QStringLiteral("PUT"), QStringLiteral("PUT") } })
                .value(QStringLiteral("POST"))
                .advanced(),
            Field(keys::WebhookAuthHeader, Text::tr("Secret header"))
                .placeholder(QStringLiteral("Authorization"))
                .advanced(),
            Field(keys::WebhookAuthValue, Text::tr("Secret header value"), "secret").advanced(),
            Field(keys::WebhookHeaders, Text::tr("Extra headers"), "multiline")
                .placeholder(QStringLiteral("Name: value"))
                .advanced(),
        };
    case ChannelType::PagerDuty:
        return {
            Field(keys::PagerDutyRoutingKey, Text::tr("Integration key"), "secret")
                .required()
                .help(Text::tr("From an Events API v2 integration on the service.")),
            Field(keys::PagerDutyRegion, Text::tr("Region"), "choice")
                .options(
                    { { QStringLiteral("us"), Text::tr("US") }, { QStringLiteral("eu"), Text::tr("EU") } })
                .value(QStringLiteral("us"))
                .advanced(),
        };
    }
    return {};
}

namespace {
QVariantMap defaultsOf(const QVariantList& fields)
{
    QVariantMap map;
    for (const QVariant& f : fields) {
        const QVariantMap m = f.toMap();
        if (m.contains(QStringLiteral("defaultValue")))
            map.insert(m.value(QStringLiteral("key")).toString(), m.value(QStringLiteral("defaultValue")));
    }
    return map;
}
}  // namespace

QVariantMap sourceDefaults(SourceType type)
{
    QVariantMap map = defaultsOf(forSource(type));
    if (type == SourceType::HttpJson)
        map.insert(QString::fromLatin1(keys::HttpRuntimeUnit), QStringLiteral("min"));
    return map;
}

QVariantMap channelDefaults(ChannelType type)
{
    return defaultsOf(forChannel(type));
}

QStringList accountKeys(SourceType type)
{
    QStringList keys;
    for (const QVariant& f : forSource(type)) {
        const QVariantMap m = f.toMap();
        if (m.value(QStringLiteral("account")).toBool())
            keys << m.value(QStringLiteral("key")).toString();
    }
    return keys;
}

}  // namespace nutty::fields
