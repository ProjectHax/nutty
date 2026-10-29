// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "app/ui/UiSetup.h"
#include "app/viewmodels/AppContext.h"
#include "common/FakeSource.h"
#include "core/engine/Monitor.h"
#include "core/storage/ConfigStore.h"
#include "core/storage/FileSecretStore.h"
#include "core/storage/HistoryStore.h"

#include <QObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QtQml/QQmlExtensionPlugin>
#include <QtQuickTest>

#include <memory>

Q_IMPORT_QML_PLUGIN(NuttyPlugin)

using namespace nutty;
using namespace nutty::test;

// Lets a test add a device with settings, and see and finish the changes
// the page sends to it.
class Probe : public QObject
{
    Q_OBJECT

public:
    explicit Probe(AppContext* context)
        : m_context(context)
    {
    }

    // A DELTA Pro-like station with a few settings, online at 80 %.
    Q_INVOKABLE QString addStation()
    {
        AppConfig config = m_context->config();
        DeviceConfig station = makeDevice(QStringLiteral("Shed station"), SourceType::EcoFlow, 30);
        m_station = station.id;
        config.devices << station;
        m_context->setConfig(config);
        FakeSource* s = FakeSource::find(m_station);
        if (!s)
            return {};
        DeviceControl ac;
        ac.id = QStringLiteral("acOutput");
        ac.value = 1;
        ac.ask = ControlAsk::Always;
        DeviceControl limit;
        limit.id = QStringLiteral("chargeLimit");
        limit.kind = ControlKind::Level;
        limit.minimum = 50;
        limit.unit = QStringLiteral("%");
        limit.value = 100;
        DeviceControl screen;
        screen.id = QStringLiteral("screenTimeout");
        screen.kind = ControlKind::Choice;
        screen.choices = { 0, 10, 30, 60 };
        screen.unit = QStringLiteral("s");
        screen.value = 30;
        s->settings = { ac, limit, screen };
        s->fakeDetails.inputs = { { QStringLiteral("ac"), 400 }, { QStringLiteral("solar"), 0 } };
        s->fakeDetails.outputs = { { QStringLiteral("ac"), 250 }, { QStringLiteral("usb"), 10 } };
        DeviceDetails::Pack pack;
        pack.id = QStringLiteral("main");
        pack.charge = 80;
        pack.health = 97;
        pack.voltage = 50;
        pack.current = 3;
        s->fakeDetails.packs = { pack };
        s->fakeDetails.energy = { { QStringLiteral("acIn"), 1200 } };
        s->push(online(80, 3600));
        emit s->controlsChanged();
        return idString(m_station);
    }

    Q_INVOKABLE void removeStation()
    {
        AppConfig config = m_context->config();
        config.devices.removeIf([this](const DeviceConfig& d) { return d.id == m_station; });
        m_context->setConfig(config);
    }

    // [{ id, value }], oldest first.
    Q_INVOKABLE QVariantList applied() const
    {
        QVariantList out;
        if (const FakeSource* s = FakeSource::find(m_station)) {
            for (const auto& [id, value] : s->applied)
                out << QVariantMap { { QStringLiteral("id"), id }, { QStringLiteral("value"), value } };
        }
        return out;
    }

    Q_INVOKABLE void finish(const QString& id, double value, bool applied)
    {
        if (FakeSource* s = FakeSource::find(m_station))
            s->finishControl(id, value, applied ? ControlResult::Applied : ControlResult::Unconfirmed);
    }

private:
    AppContext* m_context;
    QUuid m_station;
};

// The pages need the app's services: a Monitor fed by FakeSources, an
// in-memory history and a config store in a temporary folder, wired into the
// AppContext the view-models pick up.
class Setup : public QObject
{
    Q_OBJECT

public slots:
    void applicationAvailable()
    {
        nutty::prepareQuickWindows();
        nutty::selectQuickControlsStyle();
        startServices();
    }

    void qmlEngineAvailable(QQmlEngine* engine)
    {
        nutty::setupQmlEngine(*engine);
        engine->rootContext()->setContextProperty(QStringLiteral("Probe"), m_probe.get());
    }

    void cleanupTestCase()
    {
        m_probe.reset();
        m_context.reset();
        m_monitor.reset();
        m_config.reset();
        m_secrets.reset();
        m_history.reset();
    }

private:
    void startServices()
    {
        FakeSource::registerFor(SourceType::Nut);
        // Device-reported names can be long; the pickers must cope.
        FakeDiscovery::registerFor(SourceType::System,
            { { QStringLiteral("ups_fake"),
                QStringLiteral("Uninterruptible Power Supply Company Line-Interactive Tower 1500 VA "
                               "FW:COM 03.8 / UPS 03.8 / NMC 01.2 with an even longer product string"),
                QStringLiteral(
                    "UPS · Line-Interactive Tower 1500 VA, front panel revision 7, made for testing"),
                true } });
        FakeSource::registerFor(SourceType::EcoFlow);
        FakeDiscovery::registerFor(SourceType::EcoFlow,
            { { QStringLiteral("S1"), QStringLiteral("Shed station"), QStringLiteral("DELTA 2"), true },
                { QStringLiteral("S2"), QStringLiteral("Garage station"), QStringLiteral("DELTA Pro"),
                    true } });
        m_history = std::make_unique<HistoryStore>();
        m_history->open(QStringLiteral(":memory:"));
        m_secrets = std::make_unique<FileSecretStore>(m_dir.filePath(QStringLiteral("secrets.json")));
        m_config
            = std::make_unique<ConfigStore>(m_dir.filePath(QStringLiteral("nutty.ini")), m_secrets.get());
        m_monitor = std::make_unique<Monitor>(m_history.get());

        AppContext::Services services;
        services.monitor = m_monitor.get();
        services.history = m_history.get();
        services.configStore = m_config.get();
        services.secrets = m_secrets.get();
        services.dataDir = m_dir.path();
        services.dryRunForced = true;
        m_context = std::make_unique<AppContext>(services);
        m_probe = std::make_unique<Probe>(m_context.get());

        AppConfig config;
        DeviceConfig rack = makeDevice(QStringLiteral("Rack UPS"));
        DeviceConfig desk = makeDevice(QStringLiteral("Desk UPS"));
        config.devices << rack << desk;
        config.groups << DeviceGroup { QUuid::createUuid(), QStringLiteral("Office"), { rack.id, desk.id } };
        config.rules << makeRule(rack.id, ConditionType::ChargeBelow, 30, ActionType::Shutdown, 60)
                     << makeRule(rack.id, ConditionType::ForcedShutdown, 0, ActionType::Shutdown, 0)
                     << makeRule(config.groups.first().id, ConditionType::RuntimeBelow, 300,
                            ActionType::Hibernate, 90, RuleScope::GroupAll);
        ChannelConfig desktop;
        desktop.id = QUuid::createUuid();
        desktop.name = QStringLiteral("This computer");
        desktop.type = ChannelType::Desktop;
        config.channels << desktop;
        m_context->updateConfig(config);

        for (int i = 0; i < 6; ++i) {
            Event e;
            e.timestamp = QDateTime::currentDateTimeUtc().addSecs(-600 * (6 - i));
            e.type = i % 2 == 0 ? EventType::OnBattery : EventType::PowerRestored;
            e.severity = defaultSeverity(e.type);
            e.deviceId = rack.id;
            e.deviceName = rack.name;
            e.title = i % 2 == 0 ? QStringLiteral("On battery") : QStringLiteral("Power restored");
            m_history->addEvent(e);
        }
        if (FakeSource* s = FakeSource::find(rack.id))
            s->push(online(100, 1800));
        if (FakeSource* s = FakeSource::find(desk.id))
            s->push(onBattery(62, 840));
    }

    QTemporaryDir m_dir;
    std::unique_ptr<HistoryStore> m_history;
    std::unique_ptr<FileSecretStore> m_secrets;
    std::unique_ptr<ConfigStore> m_config;
    std::unique_ptr<Monitor> m_monitor;
    std::unique_ptr<AppContext> m_context;
    std::unique_ptr<Probe> m_probe;
};

QUICK_TEST_MAIN_WITH_SETUP(nutty_qml, Setup)

#include "tst_qml.moc"
