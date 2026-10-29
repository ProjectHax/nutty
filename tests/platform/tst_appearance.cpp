// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/appearance/SystemAppearance.h"
#include "platform/mac/MacActivation.h"
#include "platform/window/WindowEffects.h"

#include <QSignalSpy>
#include <QTest>
#include <QWindow>

using namespace nutty;

// Runs on the offscreen platform: nothing here may need a real desktop.
class TestAppearance : public QObject
{
    Q_OBJECT

private slots:
    void constructsOffscreen()
    {
        SystemAppearance appearance;
        QSignalSpy dark(&appearance, &SystemAppearance::darkChanged);
        // Let any async portal replies land.
        QTest::qWait(200);
        qInfo() << "dark" << appearance.isDark() << "accent" << appearance.accentColor() << "reduceMotion"
                << appearance.reduceMotion();
        if (appearance.accentColor().isValid())
            QCOMPARE(appearance.accentColor().alpha(), 255);
        // Properties are readable through the meta-object, as QML reads them.
        QCOMPARE(appearance.property("dark").toBool(), appearance.isDark());
        QCOMPARE(appearance.property("reduceMotion").toBool(), appearance.reduceMotion());
        QCOMPARE(appearance.property("accentColor").value<QColor>(), appearance.accentColor());
    }

    void forcedSchemeDoesNotChangeSystemValue()
    {
        SystemAppearance appearance;
        QTest::qWait(200);
        const bool before = appearance.isDark();
        const bool applied = SystemAppearance::setAppColorScheme(before ? ThemeMode::Light : ThemeMode::Dark);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        QVERIFY(applied);
#else
        QVERIFY(!applied);
#endif
        QTest::qWait(50);
        QCOMPARE(appearance.isDark(), before);
        SystemAppearance::setAppColorScheme(ThemeMode::System);
    }

    void windowEffects()
    {
        QVERIFY(!WindowEffects::apply(nullptr, {}));
#ifdef Q_OS_LINUX
        QWindow window;
        QVERIFY(!WindowEffects::apply(&window, { true, true }));
#endif
    }

    void macActivationIsHarmless()
    {
#ifndef Q_OS_MACOS
        MacActivation::setRegularActivationPolicy(true);
        MacActivation::setRegularActivationPolicy(false);
#endif
    }
};

QTEST_MAIN(TestAppearance)
#include "tst_appearance.moc"
