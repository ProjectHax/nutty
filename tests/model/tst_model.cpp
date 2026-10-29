// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/model/Config.h"
#include "core/model/DeviceStatus.h"
#include "core/model/Event.h"
#include "core/model/Source.h"

#include <QSignalSpy>
#include <QTest>

using namespace nutty;

namespace {

class StubSource : public Source
{
public:
    using Source::Source;
    void start() override { ++starts; }
    void stop() override { ++stops; }
    void pollNow() override { }
    QString describe() const override { return QStringLiteral("stub"); }
    void emitStatus(DeviceStatus s) { publish(std::move(s)); }
    void emitError() { reportError(SourceError::Network, QStringLiteral("boom")); }
    int starts = 0;
    int stops = 0;
};

}  // namespace

class TestModel : public QObject
{
    Q_OBJECT

private slots:
    void statusTokensRoundTrip()
    {
        const auto flags = parseStatusTokens(u"OL CHRG  lb unknown");
        QVERIFY(flags.testFlag(StatusFlag::Online));
        QVERIFY(flags.testFlag(StatusFlag::Charging));
        QVERIFY(flags.testFlag(StatusFlag::LowBattery));
        QVERIFY(!flags.testFlag(StatusFlag::OnBattery));
        QCOMPARE(statusTokens(flags), QStringLiteral("OL LB CHRG"));
        QCOMPARE(statusTokens({}), QString());
    }

    void enumKeys()
    {
        QCOMPARE(enumKey(SourceType::EcoFlow), QStringLiteral("EcoFlow"));
        QCOMPARE(enumFromKey(u"Shutdown", ActionType::Notify), ActionType::Shutdown);
        QCOMPARE(enumFromKey(u"nonsense", ActionType::Notify), ActionType::Notify);
    }

    void configDefaults()
    {
        DeviceConfig nut;
        nut.type = SourceType::Nut;
        QCOMPARE(nut.effectivePollIntervalSec(), 5);
        nut.pollIntervalSec = 12;
        QCOMPARE(nut.effectivePollIntervalSec(), 12);
        QVERIFY(defaultCommLostCritical(SourceType::Nut));
        QVERIFY(!defaultCommLostCritical(SourceType::EcoFlow));
        QCOMPARE(secretKeys(SourceType::EcoFlow).size(), 2);
        QVERIFY(defaultChannelEvents(ChannelType::Email).contains(EventType::OnBattery));
        QVERIFY(!defaultChannelEvents(ChannelType::Email).contains(EventType::Bypass));

        AppConfig cfg;
        DeviceConfig d;
        d.id = QUuid::createUuid();
        cfg.devices << d;
        QVERIFY(cfg.device(d.id));
        QVERIFY(!cfg.device(QUuid::createUuid()));
    }

    void sourceBase()
    {
        DeviceConfig cfg;
        cfg.id = QUuid::createUuid();
        StubSource src(cfg);
        QSignalSpy status(&src, &Source::statusUpdated);
        QSignalSpy state(&src, &Source::connectionStateChanged);
        QSignalSpy error(&src, &Source::errorOccurred);

        src.emitError();
        QCOMPARE(src.connectionState(), ConnectionState::Error);
        QCOMPARE(error.count(), 1);

        DeviceStatus s;
        s.charge = 80;
        src.emitStatus(s);
        QCOMPARE(status.count(), 1);
        QCOMPARE(src.connectionState(), ConnectionState::Connected);
        QVERIFY(src.lastStatus().sampledAt.isValid());
        QVERIFY(src.lastError().isEmpty());
        QCOMPARE(state.count(), 2);

        src.reset();
        QCOMPARE(src.stops, 1);
        QCOMPARE(src.starts, 1);
        QVERIFY(!src.lastStatus().isValid());
    }

    void registry()
    {
        QVERIFY(!SourceRegistry::hasFactory(SourceType::Apcupsd));
        SourceRegistry::registerFactory(SourceType::Apcupsd,
            [](const DeviceConfig& c, QObject* p) { return std::make_unique<StubSource>(c, p); });
        DeviceConfig cfg;
        cfg.type = SourceType::Apcupsd;
        auto src = SourceRegistry::create(cfg);
        QVERIFY(src);
        cfg.type = SourceType::EcoFlow;
        QVERIFY(!SourceRegistry::create(cfg));
    }

    void severities()
    {
        QCOMPARE(defaultSeverity(EventType::ForcedShutdown), Severity::Critical);
        QCOMPARE(defaultSeverity(EventType::OnBattery), Severity::Warning);
        QCOMPARE(defaultSeverity(EventType::PowerRestored), Severity::Info);
    }
};

QTEST_GUILESS_MAIN(TestModel)
#include "tst_model.moc"
