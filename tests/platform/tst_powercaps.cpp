// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/power/PowerActions.h"
#include "platform/power/SleepWatcher.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

using namespace nutty;

// Only asks what's possible. Nothing here may call PowerActions::execute()
// or runCommand(): the test machine must stay up.
class TestPowerCaps : public QObject
{
    Q_OBJECT

private slots:
    void alwaysAvailable()
    {
        QVERIFY(PowerActions::capability(ActionType::Notify).available);
        QVERIFY(PowerActions::capability(ActionType::Command).available);
    }

    void systemActionsAnswer_data()
    {
        QTest::addColumn<ActionType>("action");
        QTest::newRow("sleep") << ActionType::Sleep;
        QTest::newRow("hibernate") << ActionType::Hibernate;
        QTest::newRow("shutdown") << ActionType::Shutdown;
    }

    void systemActionsAnswer()
    {
        QFETCH(ActionType, action);
        QElapsedTimer timer;
        timer.start();
        const auto capability = PowerActions::capability(action);
        // Bounded even without a system bus or logind.
        QVERIFY(timer.elapsed() < 15000);
        if (!capability.available)
            QVERIFY2(!capability.reason.isEmpty(), "an unavailable action must say why");
        qInfo() << enumKey(action) << capability.available << capability.reason;
    }

    void sandboxProbe()
    {
        // Just has to answer; the CI and dev machines aren't sandboxed.
        const bool sandboxed = PowerActions::isSandboxed();
        Q_UNUSED(sandboxed);
    }

    void sleepWatcherConstructs()
    {
        SleepWatcher watcher;
        QSignalSpy sleeping(&watcher, &SleepWatcher::aboutToSleep);
        QSignalSpy resumed(&watcher, &SleepWatcher::resumed);
        qInfo() << "sleep notifications supported:" << watcher.isSupported();
        QCOMPARE(sleeping.size(), 0);
        QCOMPARE(resumed.size(), 0);

        // Repeated OS reports collapse into one signal per transition.
        watcher.notifyResumed();
        QCOMPARE(resumed.size(), 0);
        watcher.notifySleeping();
        watcher.notifySleeping();
        QCOMPARE(sleeping.size(), 1);
        watcher.notifyResumed();
        watcher.notifyResumed();
        QCOMPARE(resumed.size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestPowerCaps)
#include "tst_powercaps.moc"
