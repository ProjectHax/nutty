// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/power/PowerActions.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace nutty;

// The "Run command" action. The commands here only write a file in a
// temporary directory.
class TestRunCommand : public QObject
{
    Q_OBJECT

    static QByteArray contents(const QString& path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll().trimmed() : QByteArray();
    }

private slots:
    void emptyCommandIsAnError_data()
    {
        QTest::addColumn<QString>("command");
        QTest::newRow("empty") << QString();
        QTest::newRow("blank") << QStringLiteral("  \t ");
    }

    void emptyCommandIsAnError()
    {
        QFETCH(QString, command);
        const auto result = PowerActions::runCommand(command);
        QVERIFY(!result);
        QCOMPARE(result.error(), QStringLiteral("No command is set."));
    }

    // A pipe, a redirect and quotes only work through a shell.
    void runsThroughTheShell()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString out = dir.filePath(QStringLiteral("out.txt"));
#if defined(Q_OS_WIN)
        const QString command = QStringLiteral("echo ON BATTERY>%1").arg(QDir::toNativeSeparators(out));
#else
        const QString command = QStringLiteral("printf '%s' 'on battery' | tr a-z A-Z > '%1'").arg(out);
#endif
        QVERIFY(PowerActions::runCommand(command));
        QTRY_COMPARE(contents(out), QByteArray("ON BATTERY"));
    }

#if !defined(Q_OS_WIN)
    // Detached: a slow script doesn't hold nutty up.
    void doesNotWait()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString out = dir.filePath(QStringLiteral("done"));
        QElapsedTimer timer;
        timer.start();
        QVERIFY(PowerActions::runCommand(QStringLiteral("sleep 1; echo done > '%1'").arg(out)));
        QVERIFY(timer.elapsed() < 900);
        QVERIFY(!QFile::exists(out));
        QTRY_COMPARE(contents(out), QByteArray("done"));
    }
#endif
};

QTEST_GUILESS_MAIN(TestRunCommand)
#include "tst_runcommand.moc"
