// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/autostart/Autostart.h"
#include "platform/power/PowerActions.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace nutty;

// The XDG autostart entry, written into a temporary XDG_CONFIG_HOME so the
// real session is never touched.
class TestAutostart : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        if (PowerActions::isSandboxed())
            QSKIP("Under Flatpak autostart goes through the Background portal");
        QVERIFY(m_config.isValid());
        qputenv("XDG_CONFIG_HOME", QFile::encodeName(m_config.path()));
        QCOMPARE(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation), m_config.path());
    }

    void roundTrip()
    {
        QVERIFY(Autostart::isSupported());
        QVERIFY(!Autostart::isEnabled());

        auto enabled = Autostart::setEnabled(true);
        QVERIFY2(enabled.has_value(), enabled ? "" : qPrintable(enabled.error()));
        QVERIFY(Autostart::isEnabled());

        const auto entryPath = m_config.path() + QStringLiteral("/autostart/" NUTTY_APP_ID ".desktop");
        QFile entry(entryPath);
        QVERIFY(entry.open(QIODevice::ReadOnly));
        const auto text = QString::fromUtf8(entry.readAll());
        QVERIFY(text.startsWith(QStringLiteral("[Desktop Entry]")));
        QVERIFY(text.contains(QStringLiteral("Type=Application")));
        QVERIFY(text.contains(QStringLiteral(" --hidden\n")));
        entry.close();

        auto disabled = Autostart::setEnabled(false);
        QVERIFY2(disabled.has_value(), disabled ? "" : qPrintable(disabled.error()));
        QVERIFY(!Autostart::isEnabled());
        QVERIFY(!QFile::exists(entryPath));
    }

private:
    QTemporaryDir m_config;
};

QTEST_GUILESS_MAIN(TestAutostart)
#include "tst_autostart_linux.moc"
