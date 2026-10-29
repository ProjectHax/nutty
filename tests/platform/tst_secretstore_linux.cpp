// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/secrets/SystemSecretStore.h"

#include <QTest>
#include <QUuid>

using namespace nutty;

// Writes to the Secret Service, so it only runs when asked to, against a
// throwaway keyring. With HOME and XDG_DATA_HOME pointing at an empty
// directory, run inside "dbus-run-session -- sh -c '...'":
//   echo -n test | gnome-keyring-daemon --unlock --components=secrets
//   NUTTY_TEST_SECRET_SERVICE=1 ./tst_secretstore_linux
class TestSecretStore : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        if (qEnvironmentVariable("NUTTY_TEST_SECRET_SERVICE") != u"1")
            QSKIP("Set NUTTY_TEST_SECRET_SERVICE=1 with a throwaway keyring to run this");
        m_store = createSystemSecretStore();
        if (!m_store)
            QSKIP("No Secret Service is reachable");
        m_key = QStringLiteral("test/%1/password").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    }

    void cleanupTestCase()
    {
        if (m_store)
            m_store->remove(m_key);
    }

    void roundTrip()
    {
        QVERIFY(m_store->isSecure());
        QVERIFY(!m_store->backendName().isEmpty());

        auto missing = m_store->read(m_key);
        QVERIFY2(missing.has_value(), missing ? "" : qPrintable(missing.error()));
        QVERIFY(!missing->has_value());

        const auto value = QStringLiteral("pässwörd with spaces & ünïcode");
        auto written = m_store->write(m_key, value);
        QVERIFY2(written.has_value(), written ? "" : qPrintable(written.error()));
        auto read = m_store->read(m_key);
        QVERIFY2(read.has_value(), read ? "" : qPrintable(read.error()));
        QCOMPARE(read->value_or(QString()), value);

        // Writing again replaces instead of adding a second item.
        QVERIFY(m_store->write(m_key, QStringLiteral("second")).has_value());
        read = m_store->read(m_key);
        QVERIFY(read.has_value());
        QCOMPARE(read->value_or(QString()), QStringLiteral("second"));

        QVERIFY(m_store->remove(m_key).has_value());
        read = m_store->read(m_key);
        QVERIFY(read.has_value());
        QVERIFY(!read->has_value());
        // Removing a missing key is fine.
        QVERIFY(m_store->remove(m_key).has_value());
    }

    void keysAreSeparate()
    {
        const auto other = m_key + QStringLiteral("-other");
        QVERIFY(m_store->write(m_key, QStringLiteral("a")).has_value());
        QVERIFY(m_store->write(other, QStringLiteral("b")).has_value());
        QCOMPARE(m_store->read(m_key)->value_or(QString()), QStringLiteral("a"));
        QCOMPARE(m_store->read(other)->value_or(QString()), QStringLiteral("b"));
        QVERIFY(m_store->remove(other).has_value());
        QCOMPARE(m_store->read(m_key)->value_or(QString()), QStringLiteral("a"));
    }

private:
    std::unique_ptr<SecretStore> m_store;
    QString m_key;
};

QTEST_GUILESS_MAIN(TestSecretStore)
#include "tst_secretstore_linux.moc"
