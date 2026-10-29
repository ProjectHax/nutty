// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/instance/SingleInstance.h"

#include <QFile>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>

#include <atomic>
#include <thread>

using namespace nutty;

namespace {

// A key no real nutty uses, so the test never talks to a running app.
QString uniqueKey()
{
    return QStringLiteral("nutty-test-") + QUuid::createUuid().toString(QUuid::Id128).left(12);
}

// sendToPrimary() blocks, and the primary answers from this thread's event
// loop, so the send runs on another thread while the test spins the loop.
bool sendFromThread(SingleInstance& secondary, const QByteArray& message)
{
    std::atomic<int> result { -1 };
    std::thread sender([&] { result = secondary.sendToPrimary(message, 5000) ? 1 : 0; });
    const bool done = QTest::qWaitFor([&] { return result != -1; }, 10000);
    sender.join();
    return done && result == 1;
}

}  // namespace

class TestSingleInstance : public QObject
{
    Q_OBJECT

private slots:
    void primaryReceivesMessages()
    {
        const auto key = uniqueKey();
        SingleInstance primary(key);
        QVERIFY(primary.isPrimary());
        QSignalSpy received(&primary, &SingleInstance::messageReceived);

        SingleInstance secondary(key);
        QVERIFY(!secondary.isPrimary());
        QVERIFY(sendFromThread(secondary, "show"));
        QTRY_COMPARE(received.size(), 1);
        QCOMPARE(received.first().first().toByteArray(), QByteArray("show"));

        // Several messages, including an empty one and a larger one.
        QVERIFY(sendFromThread(secondary, QByteArray()));
        QVERIFY(sendFromThread(secondary, QByteArray(20000, 'x')));
        QTRY_COMPARE(received.size(), 3);
        QCOMPARE(received.at(1).first().toByteArray(), QByteArray());
        QCOMPARE(received.at(2).first().toByteArray(), QByteArray(20000, 'x'));
    }

    void primaryDoesNotSendToItself()
    {
        SingleInstance primary(uniqueKey());
        QVERIFY(primary.isPrimary());
        QVERIFY(!primary.sendToPrimary("show", 200));
    }

    void oversizedMessageIsRefused()
    {
        const auto key = uniqueKey();
        SingleInstance primary(key);
        SingleInstance secondary(key);
        QVERIFY(!secondary.sendToPrimary(QByteArray(128 * 1024, 'x'), 200));
    }

    void lockIsReleased()
    {
        const auto key = uniqueKey();
        {
            SingleInstance first(key);
            QVERIFY(first.isPrimary());
        }
        SingleInstance second(key);
        QVERIFY(second.isPrimary());
    }

    void staleSocketIsReplaced()
    {
#ifdef Q_OS_WIN
        QSKIP("Named pipes leave nothing behind");
#else
        const auto key = uniqueKey();
        const auto socketPath = SingleInstance::runtimeDirectory() + u'/' + key + QStringLiteral(".socket");
        {
            QFile leftover(socketPath);
            QVERIFY(leftover.open(QIODevice::WriteOnly));
        }
        SingleInstance primary(key);
        QVERIFY(primary.isPrimary());
        QSignalSpy received(&primary, &SingleInstance::messageReceived);
        SingleInstance secondary(key);
        QVERIFY(sendFromThread(secondary, "again"));
        QTRY_COMPARE(received.size(), 1);
#endif
    }

    void secondaryWithoutListenerFails()
    {
        // A lock held by a live process that isn't listening (e.g. still
        // starting up): sending times out instead of hanging.
        const auto key = uniqueKey();
        SingleInstance primary(key);
        QVERIFY(primary.isPrimary());
        SingleInstance secondary(key);
        // Nothing processes the primary's events here, so no ack arrives.
        QVERIFY(!secondary.sendToPrimary("show", 300));
    }
};

QTEST_GUILESS_MAIN(TestSingleInstance)
#include "tst_singleinstance.moc"
