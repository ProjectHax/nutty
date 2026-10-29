// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/storage/ConfigStore.h"
#include "core/storage/FileSecretStore.h"

#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace nutty;

namespace {

// Counts keychain traffic so tests can check that saves only touch what changed.
class MemorySecretStore final : public SecretStore
{
public:
    bool isSecure() const override { return true; }
    QString backendName() const override { return QStringLiteral("memory"); }
    std::expected<std::optional<QString>, QString> read(const QString& key) override
    {
        ++reads;
        if (!values.contains(key))
            return std::optional<QString>();
        return std::optional<QString>(values.value(key));
    }
    std::expected<void, QString> write(const QString& key, const QString& value) override
    {
        ++writes;
        values.insert(key, value);
        return {};
    }
    std::expected<void, QString> remove(const QString& key) override
    {
        ++removes;
        values.remove(key);
        return {};
    }

    QHash<QString, QString> values;
    int reads = 0;
    int writes = 0;
    int removes = 0;
};

QString readAll(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll());
}

AppConfig sampleConfig()
{
    AppConfig c;
    c.general.theme = ThemeMode::Dark;
    c.general.trayIcon = TrayIconStyle::Battery;
    c.general.language = QStringLiteral("de");
    c.general.sampleRetentionDays = 14;
    c.actions.safetyMarginSec = 45;
    c.actions.fsdDelaySec = 2;

    DeviceConfig nut;
    nut.id = QUuid::createUuid();
    nut.name = QStringLiteral("Rack UPS");
    nut.type = SourceType::Nut;
    nut.pollIntervalSec = 2;
    nut.kind = DeviceKind::RackUps;
    nut.settings = { { QString::fromLatin1(keys::NutHost), QStringLiteral("192.0.2.10") },
        { QString::fromLatin1(keys::NutPort), 3493 },
        { QString::fromLatin1(keys::NutUps), QStringLiteral("ups") },
        { QString::fromLatin1(keys::NutUsername), QStringLiteral("monuser") } };
    nut.secrets = { { QString::fromLatin1(keys::NutPassword), QStringLiteral("s3cret-nut-pass") } };
    nut.notifyChargeBelow = 40;
    nut.commLostCritical = true;

    DeviceConfig eco;
    eco.id = QUuid::createUuid();
    eco.name = QStringLiteral("Power station");
    eco.type = SourceType::EcoFlow;
    eco.enabled = false;
    eco.settings = { { QString::fromLatin1(keys::EcoSerial), QStringLiteral("SN0000") } };
    eco.secrets = { { QString::fromLatin1(keys::EcoAccessKey), QStringLiteral("eco-access-value") },
        { QString::fromLatin1(keys::EcoSecretKey), QStringLiteral("eco-secret-value") } };
    eco.commLostCritical = false;
    c.devices << nut << eco;

    c.groups << DeviceGroup { QUuid::createUuid(), QStringLiteral("All"), { nut.id, eco.id } };

    Rule r;
    r.id = QUuid::createUuid();
    r.name = QStringLiteral("Low");
    r.scope = RuleScope::GroupAll;
    r.target = c.groups.first().id;
    r.condition = ConditionType::RuntimeBelow;
    r.threshold = 300;
    r.action = ActionType::Command;
    r.countdownSec = 90;
    r.command = QStringLiteral("echo hi");
    c.rules << r;

    ChannelConfig push;
    push.id = QUuid::createUuid();
    push.name = QStringLiteral("Phone");
    push.type = ChannelType::Pushover;
    push.events = { EventType::OnBattery, EventType::ActionScheduled };
    push.settings = { { QString::fromLatin1(keys::PushoverSound), QStringLiteral("siren") } };
    push.secrets = { { QString::fromLatin1(keys::PushoverUserKey), QStringLiteral("user-key-value") },
        { QString::fromLatin1(keys::PushoverApiToken), QStringLiteral("api-token-value") } };
    ChannelConfig desktop;
    desktop.id = QUuid::createUuid();
    desktop.name = QStringLiteral("Desktop");
    desktop.type = ChannelType::Desktop;
    desktop.events = {};  // explicitly nothing
    c.channels << push << desktop;
    return c;
}

}  // namespace

class TestConfigStore : public QObject
{
    Q_OBJECT

private slots:
    void defaultsWhenMissing()
    {
        QTemporaryDir dir;
        ConfigStore store(dir.filePath(QStringLiteral("nutty.ini")), nullptr);
        QVERIFY(!store.exists());
        const auto c = store.load();
        QVERIFY(store.lastError().isEmpty());
        QCOMPARE(c.schemaVersion, AppConfig::kSchemaVersion);
        QVERIFY(c.devices.isEmpty());
        QCOMPARE(c.channels.size(), 1);
        QCOMPARE(c.channels.first().type, ChannelType::Desktop);
        QVERIFY(c.channels.first().events.contains(EventType::OnBattery));
    }

    void pagerDutyKeyGoesToTheKeychain()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("nutty.ini"));
        MemorySecretStore secrets;
        AppConfig config;
        ChannelConfig pd;
        pd.id = QUuid::createUuid();
        pd.type = ChannelType::PagerDuty;
        pd.settings = { { QString::fromLatin1(keys::PagerDutyRegion), QStringLiteral("eu") } };
        pd.secrets
            = { { QString::fromLatin1(keys::PagerDutyRoutingKey), QStringLiteral("integration-key-value") } };
        config.channels << pd;
        {
            ConfigStore store(path, &secrets);
            QVERIFY(store.save(config));
        }
        QVERIFY(!readAll(path).contains(QStringLiteral("integration-key-value")));
        QCOMPARE(secrets.values.value(ConfigStore::channelSecretKey(pd.id, QStringLiteral("routingKey"))),
            QStringLiteral("integration-key-value"));

        ConfigStore store(path, &secrets);
        const auto loaded = store.load();
        QCOMPARE(loaded.channels.size(), 1);
        QCOMPARE(loaded.channels.first().type, ChannelType::PagerDuty);
        QCOMPARE(
            loaded.channels.first().settings.value(QString::fromLatin1(keys::PagerDutyRegion)).toString(),
            QStringLiteral("eu"));
        QCOMPARE(
            loaded.channels.first().secrets.value(QString::fromLatin1(keys::PagerDutyRoutingKey)).toString(),
            QStringLiteral("integration-key-value"));
    }

    void roundTripKeepsSecretsOutOfTheIni()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("sub/nutty.ini"));
        MemorySecretStore secrets;
        const auto original = sampleConfig();
        {
            ConfigStore store(path, &secrets);
            QVERIFY(store.save(original));
            QVERIFY2(store.lastError().isEmpty(), qPrintable(store.lastError()));
        }

        const auto ini = readAll(path);
        QVERIFY(!ini.isEmpty());
        for (const auto* secret : { "s3cret-nut-pass", "eco-access-value", "eco-secret-value",
                 "user-key-value", "api-token-value" }) {
            QVERIFY2(!ini.contains(QLatin1StringView(secret)), secret);
        }
        QVERIFY(ini.contains(QStringLiteral("192.0.2.10")));
        const auto& nut = original.devices.at(0);
        const auto& push = original.channels.at(0);
        QCOMPARE(secrets.values.value(ConfigStore::deviceSecretKey(nut.id, QStringLiteral("password"))),
            QStringLiteral("s3cret-nut-pass"));
        QCOMPARE(secrets.values.value(ConfigStore::channelSecretKey(push.id, QStringLiteral("apiToken"))),
            QStringLiteral("api-token-value"));
        QVERIFY(ConfigStore::deviceSecretKey(nut.id, QStringLiteral("password"))
                .startsWith(QStringLiteral("device/") + nut.id.toString(QUuid::WithoutBraces)));
        QCOMPARE(secrets.values.size(), 5);

        ConfigStore store(path, &secrets);
        const auto c = store.load();
        QVERIFY2(store.lastError().isEmpty(), qPrintable(store.lastError()));
        QCOMPARE(c.general.theme, ThemeMode::Dark);
        QCOMPARE(c.general.trayIcon, TrayIconStyle::Battery);
        QCOMPARE(c.general.language, QStringLiteral("de"));
        QCOMPARE(c.general.sampleRetentionDays, 14);
        QCOMPARE(c.actions.safetyMarginSec, 45);
        QCOMPARE(c.actions.fsdDelaySec, 2);

        QCOMPARE(c.devices.size(), 2);
        const auto& d = c.devices.at(0);
        QCOMPARE(d.id, nut.id);
        QCOMPARE(d.name, nut.name);
        QCOMPARE(d.type, SourceType::Nut);
        QCOMPARE(d.pollIntervalSec, 2);
        QCOMPARE(d.kind, DeviceKind::RackUps);
        QCOMPARE(d.settings.value(QStringLiteral("host")).toString(), QStringLiteral("192.0.2.10"));
        QCOMPARE(d.settings.value(QStringLiteral("port")).toInt(), 3493);
        QVERIFY(!d.settings.contains(QStringLiteral("password")));
        QCOMPARE(d.secrets, nut.secrets);
        QCOMPARE(d.notifyChargeBelow, 40);
        const auto& e = c.devices.at(1);
        QVERIFY(!e.enabled);
        QVERIFY(!e.commLostCritical);
        QCOMPARE(e.secrets, original.devices.at(1).secrets);

        QCOMPARE(c.groups.size(), 1);
        QCOMPARE(c.groups.first().members, original.groups.first().members);

        QCOMPARE(c.rules.size(), 1);
        const auto& r = c.rules.first();
        QCOMPARE(r.id, original.rules.first().id);
        QCOMPARE(r.scope, RuleScope::GroupAll);
        QCOMPARE(r.condition, ConditionType::RuntimeBelow);
        QCOMPARE(r.threshold, 300.0);
        QCOMPARE(r.action, ActionType::Command);
        QCOMPARE(r.countdownSec, 90);
        QCOMPARE(r.command, QStringLiteral("echo hi"));

        QCOMPARE(c.channels.size(), 2);
        QCOMPARE(c.channels.at(0).events, push.events);
        QCOMPARE(c.channels.at(0).secrets, push.secrets);
        QCOMPARE(
            c.channels.at(0).settings.value(QStringLiteral("sound")).toString(), QStringLiteral("siren"));
        QVERIFY(c.channels.at(1).events.isEmpty());
    }

    void savesOnlyChangedSecrets()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("nutty.ini"));
        MemorySecretStore secrets;
        ConfigStore store(path, &secrets);
        auto c = sampleConfig();
        QVERIFY(store.save(c));
        QCOMPARE(secrets.writes, 5);
        QVERIFY(store.save(c));
        QCOMPARE(secrets.writes, 5);
        QCOMPARE(secrets.removes, 0);

        c.devices[0].secrets[QStringLiteral("password")] = QStringLiteral("changed");
        QVERIFY(store.save(c));
        QCOMPARE(secrets.writes, 6);

        // Clearing a secret removes it.
        c.channels[0].secrets.remove(QStringLiteral("userKey"));
        QVERIFY(store.save(c));
        QCOMPARE(secrets.removes, 1);
        QVERIFY(!secrets.values.contains(
            ConfigStore::channelSecretKey(c.channels[0].id, QStringLiteral("userKey"))));
    }

    void removedDevicesAndChannelsLoseTheirSecrets()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("nutty.ini"));
        MemorySecretStore secrets;
        auto c = sampleConfig();
        {
            ConfigStore store(path, &secrets);
            QVERIFY(store.save(c));
        }
        QCOMPARE(secrets.values.size(), 5);

        // A fresh store (as after a restart) learns the ids from load().
        ConfigStore store(path, &secrets);
        c = store.load();
        const auto eco = c.devices.takeAt(1);
        const auto push = c.channels.takeAt(0);
        QVERIFY(store.save(c));
        QCOMPARE(secrets.values.size(), 1);
        QVERIFY(!secrets.values.contains(ConfigStore::deviceSecretKey(eco.id, QStringLiteral("accessKey"))));
        QVERIFY(!secrets.values.contains(ConfigStore::channelSecretKey(push.id, QStringLiteral("userKey"))));

        // A type change drops the old type's secrets.
        c.devices[0].type = SourceType::Apcupsd;
        c.devices[0].secrets.clear();
        QVERIFY(store.save(c));
        QVERIFY(secrets.values.isEmpty());
    }

    void secretInIniMovesToStore()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("nutty.ini"));
        const auto id = QUuid::createUuid();
        {
            QSettings s(path, QSettings::IniFormat);
            s.setValue(QStringLiteral("schemaVersion"), 1);
            s.beginWriteArray(QStringLiteral("devices"), 1);
            s.setArrayIndex(0);
            s.setValue(QStringLiteral("id"), id.toString());
            s.setValue(QStringLiteral("type"), QStringLiteral("Nut"));
            s.setValue(QStringLiteral("settings/host"), QStringLiteral("192.0.2.1"));
            s.setValue(QStringLiteral("settings/password"), QStringLiteral("hand-edited"));
            s.endArray();
        }
        MemorySecretStore secrets;
        ConfigStore store(path, &secrets);
        auto c = store.load();
        QCOMPARE(c.devices.size(), 1);
        QCOMPARE(c.devices.first().secrets.value(QStringLiteral("password")).toString(),
            QStringLiteral("hand-edited"));
        QVERIFY(!c.devices.first().settings.contains(QStringLiteral("password")));
        // Defaults for keys the file doesn't have.
        QVERIFY(c.devices.first().enabled);
        QVERIFY(c.devices.first().commLostCritical);

        QVERIFY(store.save(c));
        QVERIFY(!readAll(path).contains(QStringLiteral("hand-edited")));
        QCOMPARE(secrets.values.value(ConfigStore::deviceSecretKey(id, QStringLiteral("password"))),
            QStringLiteral("hand-edited"));
    }

    void olderSchemaIsMigrated()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("nutty.ini"));
        {
            QSettings s(path, QSettings::IniFormat);
            s.setValue(QStringLiteral("general/theme"), QStringLiteral("Light"));
            // no schemaVersion: a pre-release file
        }
        ConfigStore store(path, nullptr);
        const auto c = store.load();
        QCOMPARE(c.schemaVersion, AppConfig::kSchemaVersion);
        QCOMPARE(c.general.theme, ThemeMode::Light);
        QCOMPARE(c.general.trayIcon, TrayIconStyle::Acorn);
        QSettings s(path, QSettings::IniFormat);
        QCOMPARE(s.value(QStringLiteral("schemaVersion")).toInt(), AppConfig::kSchemaVersion);
    }

    void withoutSecretStoreSecretsStayInMemory()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("nutty.ini"));
        ConfigStore store(path, nullptr);
        QVERIFY(store.save(sampleConfig()));
        QVERIFY(!readAll(path).contains(QStringLiteral("s3cret-nut-pass")));
        QVERIFY(store.load().devices.first().secrets.isEmpty());
    }

    void fileSecretStore()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("secrets/secrets.json"));
        {
            FileSecretStore s(path);
            QVERIFY(!s.isSecure());
            QVERIFY(!s.backendName().isEmpty());
            auto missing = s.read(QStringLiteral("device/x/password"));
            QVERIFY(missing);
            QVERIFY(!*missing);
            QVERIFY(s.write(QStringLiteral("device/x/password"), QStringLiteral("päss \"q\"")));
            QVERIFY(s.write(QStringLiteral("channel/y/token"), QStringLiteral("t")));
            QVERIFY(s.remove(QStringLiteral("channel/y/token")));
            QVERIFY(s.remove(QStringLiteral("never/existed")));
        }
        const auto perms = QFile::permissions(path);
        QVERIFY(perms.testFlag(QFileDevice::ReadOwner));
        QVERIFY(perms.testFlag(QFileDevice::WriteOwner));
#ifndef Q_OS_WIN
        QVERIFY(!(perms
            & (QFileDevice::ReadGroup | QFileDevice::ReadOther | QFileDevice::WriteGroup
                | QFileDevice::WriteOther)));
#endif
        // Not stored in plain text.
        QVERIFY(!readAll(path).contains(QStringLiteral("päss")));

        FileSecretStore again(path);
        const auto value = again.read(QStringLiteral("device/x/password"));
        QVERIFY(value && *value);
        QCOMPARE(**value, QStringLiteral("päss \"q\""));
        const auto removed = again.read(QStringLiteral("channel/y/token"));
        QVERIFY(removed && !*removed);
    }

    void configStoreWithFileSecrets()
    {
        QTemporaryDir dir;
        FileSecretStore secrets(dir.filePath(QStringLiteral("secrets.json")));
        const auto path = dir.filePath(QStringLiteral("nutty.ini"));
        const auto original = sampleConfig();
        QVERIFY(ConfigStore(path, &secrets).save(original));
        FileSecretStore reopened(dir.filePath(QStringLiteral("secrets.json")));
        ConfigStore store(path, &reopened);
        const auto c = store.load();
        QCOMPARE(c.devices.first().secrets, original.devices.first().secrets);
        QCOMPARE(c.channels.first().secrets, original.channels.first().secrets);
    }
};

QTEST_GUILESS_MAIN(TestConfigStore)
#include "tst_configstore.moc"
