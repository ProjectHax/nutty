// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/storage/HistoryStore.h"

#include <QFile>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

using namespace nutty;

namespace {

const QDateTime kBase(QDate(2026, 3, 1), QTime(12, 0), QTimeZone::utc());

Event makeEvent(EventType type, const QUuid& device, const QDateTime& at, const QString& title,
    const QString& message = {})
{
    Event e;
    e.timestamp = at;
    e.type = type;
    e.severity = defaultSeverity(type);
    e.deviceId = device;
    e.deviceName = QStringLiteral("UPS-A");
    e.title = title;
    e.message = message.isEmpty() ? title : message;
    return e;
}

DeviceStatus makeSample(const QDateTime& at, double charge, bool onBattery = false)
{
    DeviceStatus s;
    s.sampledAt = at;
    s.flags = onBattery ? StatusFlags(StatusFlag::OnBattery) : StatusFlags(StatusFlag::Online);
    s.charge = charge;
    s.runtime = static_cast<qint64>(charge * 60);
    s.load = 25;
    return s;
}

}  // namespace

class TestHistoryStore : public QObject
{
    Q_OBJECT

private:
    QUuid a = QUuid::createUuid();
    QUuid b = QUuid::createUuid();

private slots:
    void openInMemory()
    {
        HistoryStore h;
        QVERIFY(!h.isOpen());
        QCOMPARE(h.addEvent(makeEvent(EventType::Test, a, kBase, QStringLiteral("x"))), 0);
        QVERIFY2(h.open(QStringLiteral(":memory:")), qPrintable(h.lastError()));
        QVERIFY(h.isOpen());
        // A second store has its own connection.
        HistoryStore other;
        QVERIFY(other.open(QStringLiteral(":memory:")));
        h.addEvent(makeEvent(EventType::Test, a, kBase, QStringLiteral("x")));
        QCOMPARE(h.countEvents({}), 1);
        QCOMPARE(other.countEvents({}), 0);
        h.close();
        QVERIFY(!h.isOpen());
    }

    void eventsRoundTrip()
    {
        HistoryStore h;
        QVERIFY(h.open(QStringLiteral(":memory:")));
        QSignalSpy added(&h, &HistoryStore::eventAdded);

        auto e = makeEvent(EventType::OnBattery, a, kBase, QStringLiteral("On battery"),
            QStringLiteral("UPS-A is on battery (87 %, 24 min left)"));
        e.flags = StatusFlags(StatusFlag::OnBattery) | StatusFlag::Discharging;
        e.charge = 87;
        e.runtime = 1440;
        e.extra.insert(QStringLiteral("rule"), QStringLiteral("low"));
        const auto id = h.addEvent(e);
        QVERIFY(id > 0);
        QCOMPARE(added.size(), 1);
        QCOMPARE(added.first().first().value<Event>().id, id);

        const auto list = h.events({});
        QCOMPARE(list.size(), 1);
        const auto& got = list.first();
        QCOMPARE(got.id, id);
        QCOMPARE(got.timestamp, kBase);
        QCOMPARE(got.type, EventType::OnBattery);
        QCOMPARE(got.severity, Severity::Warning);
        QCOMPARE(got.deviceId, a);
        QCOMPARE(got.deviceName, QStringLiteral("UPS-A"));
        QCOMPARE(got.message, e.message);
        QCOMPARE(got.flags, e.flags);
        QCOMPARE(got.charge, std::optional<double>(87));
        QCOMPARE(got.runtime, std::optional<qint64>(1440));
        QVERIFY(!got.load);
        QCOMPARE(got.extra.value(QStringLiteral("rule")).toString(), QStringLiteral("low"));
    }

    void eventQueries()
    {
        HistoryStore h;
        QVERIFY(h.open(QStringLiteral(":memory:")));
        for (int i = 0; i < 10; ++i)
            h.addEvent(makeEvent(i % 2 ? EventType::OnBattery : EventType::PowerRestored, i < 5 ? a : b,
                kBase.addSecs(i * 60), QStringLiteral("event %1").arg(i)));
        h.addEvent(makeEvent(EventType::Alarm, a, kBase.addSecs(3600), QStringLiteral("100% load_now")));

        QCOMPARE(h.countEvents({}), 11);
        EventQuery q;
        q.devices = { a };
        QCOMPARE(h.countEvents(q), 6);
        q.types = { EventType::OnBattery };
        QCOMPARE(h.countEvents(q), 2);  // i = 1, 3

        EventQuery range;
        range.from = kBase.addSecs(120);
        range.to = kBase.addSecs(300);
        QCOMPARE(h.countEvents(range), 4);

        // Newest first by default, with paging.
        EventQuery page;
        page.limit = 3;
        auto list = h.events(page);
        QCOMPARE(list.size(), 3);
        QCOMPARE(list.at(0).title, QStringLiteral("100% load_now"));
        QCOMPARE(list.at(1).title, QStringLiteral("event 9"));
        page.offset = 3;
        QCOMPARE(h.events(page).first().title, QStringLiteral("event 7"));
        page = {};
        page.newestFirst = false;
        page.limit = 0;  // no limit
        list = h.events(page);
        QCOMPARE(list.size(), 11);
        QCOMPARE(list.first().title, QStringLiteral("event 0"));

        // Search is a literal substring: % and _ aren't wildcards.
        EventQuery search;
        search.search = QStringLiteral("event 4");
        QCOMPARE(h.countEvents(search), 1);
        search.search = QStringLiteral("100%");
        QCOMPARE(h.countEvents(search), 1);
        search.search = QStringLiteral("%");
        QCOMPARE(h.countEvents(search), 1);
        search.search = QStringLiteral("t_");
        QCOMPARE(h.countEvents(search), 0);
        search.search = QStringLiteral("ups-a");  // device name, case-insensitive
        QCOMPARE(h.countEvents(search), 11);
    }

    void episodes()
    {
        HistoryStore h;
        QVERIFY(h.open(QStringLiteral(":memory:")));
        QSignalSpy changed(&h, &HistoryStore::episodeChanged);
        const auto id = h.openEpisode(a, QStringLiteral("UPS-A"), kBase, 95, 1800);
        QVERIFY(id > 0);
        h.updateEpisode(id, 80, 1500);
        h.updateEpisode(id, 85, std::nullopt);  // higher: ignored
        h.appendEpisodeAction(id, kBase.addSecs(60), QStringLiteral("Low battery"));
        h.appendEpisodeAction(id, kBase.addSecs(61), QStringLiteral("Shut down in 60 s"));
        QVERIFY(changed.size() >= 4);

        auto e = h.episode(id);
        QVERIFY(e);
        QVERIFY(e->ongoing());
        QCOMPARE(e->deviceId, a);
        QCOMPARE(e->startCharge, std::optional<double>(95));
        QCOMPARE(e->minCharge, std::optional<double>(80));
        QCOMPARE(e->minRuntime, std::optional<qint64>(1500));
        QCOMPARE(e->actions.size(), 2);
        QCOMPARE(e->actions.at(1).text, QStringLiteral("Shut down in 60 s"));
        QCOMPARE(e->actions.at(1).timestamp, kBase.addSecs(61));
        QCOMPARE(e->durationSec(kBase.addSecs(100)), 100);

        h.closeEpisode(id, kBase.addSecs(300));
        h.closeEpisode(id, kBase.addSecs(900));  // already closed: no change
        e = h.episode(id);
        QVERIFY(!e->ongoing());
        QCOMPARE(e->end, kBase.addSecs(300));
        QCOMPARE(e->durationSec(), 300);
        QVERIFY(!h.episode(id + 100));
    }

    void episodeRanges()
    {
        HistoryStore h;
        QVERIFY(h.open(QStringLiteral(":memory:")));
        // a: [0, 10 min], [60, 70 min]; b: [30 min, ongoing]
        const auto e1 = h.openEpisode(a, {}, kBase, 100, {});
        h.closeEpisode(e1, kBase.addSecs(600));
        const auto e2 = h.openEpisode(a, {}, kBase.addSecs(3600), 100, {});
        h.closeEpisode(e2, kBase.addSecs(4200));
        const auto e3 = h.openEpisode(b, {}, kBase.addSecs(1800), 100, {});

        QCOMPARE(h.countEpisodes({}, {}, {}), 3);
        QCOMPARE(h.countEpisodes(a, {}, {}), 2);
        // Overlap: [5 min, 40 min] touches e1 and e3.
        const auto list = h.episodes({}, kBase.addSecs(300), kBase.addSecs(2400));
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(0).id, e3);  // newest first
        QCOMPARE(list.at(1).id, e1);
        // Ongoing episodes overlap everything after their start.
        QCOMPARE(h.countEpisodes(b, kBase.addSecs(86400), {}), 1);
        QCOMPARE(h.episodes(a, {}, {}, 1, 1).first().id, e1);
    }

    void closeOpenEpisodesAtLastSample()
    {
        HistoryStore h;
        QVERIFY(h.open(QStringLiteral(":memory:")));
        const auto withSamples = h.openEpisode(a, {}, kBase, 100, {});
        const auto without = h.openEpisode(b, {}, kBase.addSecs(10), 100, {});
        h.addSample(a, makeSample(kBase.addSecs(30), 90, true));
        h.addSample(a, makeSample(kBase.addSecs(90), 80, true));
        h.closeOpenEpisodes();
        QCOMPARE(h.episode(withSamples)->end, kBase.addSecs(90));
        QCOMPARE(h.episode(without)->end, kBase.addSecs(10));
    }

    void samplesBatchedAndQueried()
    {
        HistoryStore h;
        QVERIFY(h.open(QStringLiteral(":memory:")));
        for (int i = 0; i < 250; ++i)
            h.addSample(a, makeSample(kBase.addSecs(i * 60), 100 - i * 0.2, i >= 100 && i < 110));
        h.addSample(b, makeSample(kBase, 50));

        // Queries see unflushed rows too.
        auto all = h.samples(a, {}, {});
        QCOMPARE(all.size(), 250);
        QCOMPARE(all.first().timestamp, kBase);
        QCOMPARE(all.first().charge, std::optional<double>(100));
        QCOMPARE(all.first().runtime, std::optional<double>(6000));
        QCOMPARE(all.first().load, std::optional<double>(25));
        QVERIFY(!all.first().wattsIn);
        QVERIFY(!all.first().onBattery);
        QVERIFY(all.at(105).onBattery);

        const auto range = h.samples(a, kBase.addSecs(600), kBase.addSecs(1200));
        QCOMPARE(range.size(), 11);

        // The same timestamp twice replaces the row.
        h.addSample(b, makeSample(kBase, 55));
        const auto replaced = h.samples(b, {}, {});
        QCOMPARE(replaced.size(), 1);
        QCOMPARE(replaced.first().charge, std::optional<double>(55));
    }

    void samplesBucketed()
    {
        HistoryStore h;
        QVERIFY(h.open(QStringLiteral(":memory:")));
        for (int i = 0; i <= 100; ++i)
            h.addSample(a, makeSample(kBase.addSecs(i * 60), i, i == 55));
        const auto from = kBase;
        const auto to = kBase.addSecs(100 * 60);
        const auto points = h.samples(a, from, to, 10);
        QVERIFY(points.size() <= 10);
        QVERIFY(points.size() >= 9);
        // Averages stay within the data, in order, and the outage shows up.
        double last = -1;
        int onBattery = 0;
        for (const auto& p : points) {
            QVERIFY(p.charge);
            QVERIFY(*p.charge > last);
            last = *p.charge;
            QVERIFY(p.timestamp >= from && p.timestamp <= to);
            onBattery += p.onBattery ? 1 : 0;
        }
        QCOMPARE(onBattery, 1);
        QCOMPARE(points.first().charge.value_or(-1) < 10, true);
        QCOMPARE(points.last().charge.value_or(-1) > 90, true);

        // Fewer samples than points: one per sample.
        QCOMPARE(h.samples(a, from, kBase.addSecs(5 * 60), 100).size(), 6);
    }

    void prune()
    {
        HistoryStore h;
        QVERIFY(h.open(QStringLiteral(":memory:")));
        const auto now = kBase.addDays(400);
        h.addEvent(makeEvent(EventType::Test, a, kBase, QStringLiteral("old")));
        h.addEvent(makeEvent(EventType::Test, a, now.addDays(-10), QStringLiteral("recent")));
        h.addSample(a, makeSample(now.addDays(-40), 50));
        h.addSample(a, makeSample(now.addDays(-1), 60));
        const auto oldEpisode = h.openEpisode(a, {}, kBase, 100, {});
        h.closeEpisode(oldEpisode, kBase.addSecs(60));
        const auto ongoing = h.openEpisode(b, {}, kBase, 100, {});

        h.prune(30, 365, now);
        QCOMPARE(h.countEvents({}), 1);
        QCOMPARE(h.events({}).first().title, QStringLiteral("recent"));
        QCOMPARE(h.samples(a, {}, {}).size(), 1);
        QVERIFY(!h.episode(oldEpisode));
        QVERIFY(h.episode(ongoing));  // never pruned while open

        // <= 0 keeps forever.
        h.prune(0, 0, now.addDays(5000));
        QCOMPARE(h.countEvents({}), 1);
        QCOMPARE(h.samples(a, {}, {}).size(), 1);
    }

    void forgetDevice()
    {
        HistoryStore h;
        QVERIFY(h.open(QStringLiteral(":memory:")));
        h.addEvent(makeEvent(EventType::Test, a, kBase, QStringLiteral("a")));
        h.addEvent(makeEvent(EventType::Test, b, kBase, QStringLiteral("b")));
        h.openEpisode(a, {}, kBase, 100, {});
        h.addSample(a, makeSample(kBase, 50));
        h.forgetDevice(a);
        QCOMPARE(h.countEvents({}), 1);
        QCOMPARE(h.countEpisodes(a, {}, {}), 0);
        QVERIFY(h.samples(a, {}, {}).isEmpty());
    }

    void persistsAndMigrates()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = dir.filePath(QStringLiteral("history.db"));
        {
            HistoryStore h;
            QVERIFY2(h.open(path), qPrintable(h.lastError()));
            h.addEvent(makeEvent(EventType::Test, a, kBase, QStringLiteral("kept")));
            h.addSample(a, makeSample(kBase, 42));  // flushed on close
        }
        {
            HistoryStore h;
            QVERIFY(h.open(path));
            QCOMPARE(h.countEvents({}), 1);
            QCOMPARE(h.samples(a, {}, {}).size(), 1);
        }

        // A database from a newer nutty is refused, not damaged.
        {
            const auto name = QStringLiteral("tst-bump");
            {
                auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
                db.setDatabaseName(path);
                QVERIFY(db.open());
                QSqlQuery q(db);
                QVERIFY(q.exec(QStringLiteral("UPDATE schema_version SET version = %1")
                        .arg(HistoryStore::kSchemaVersion + 1)));
                db.close();
            }
            QSqlDatabase::removeDatabase(name);
        }
        HistoryStore h;
        QVERIFY(!h.open(path));
        QVERIFY(!h.lastError().isEmpty());
        QVERIFY(!h.isOpen());
    }

    void upgradesVersion1()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("history.db"));
        // A database as nutty 0.1 left it.
        const auto name = QStringLiteral("tst-v1");
        {
            auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral("CREATE TABLE schema_version (version INTEGER NOT NULL)")));
            QVERIFY(q.exec(QStringLiteral("INSERT INTO schema_version (version) VALUES (1)")));
            QVERIFY(q.exec(QStringLiteral(
                "CREATE TABLE samples (device_id TEXT NOT NULL, ts INTEGER NOT NULL, charge REAL,"
                " runtime INTEGER, load REAL, watts_in REAL, watts_out REAL, input_voltage REAL,"
                " flags INTEGER, PRIMARY KEY (device_id, ts)) WITHOUT ROWID")));
            QVERIFY(q.exec(
                QStringLiteral("INSERT INTO samples (device_id, ts, charge, flags) VALUES ('%1', %2, 64, 1)")
                    .arg(a.toString(QUuid::WithoutBraces))
                    .arg(kBase.toMSecsSinceEpoch())));
            db.close();
        }
        QSqlDatabase::removeDatabase(name);

        HistoryStore h;
        QVERIFY2(h.open(path), qPrintable(h.lastError()));
        auto old = h.samples(a, {}, {});
        QCOMPARE(old.size(), 1);
        QCOMPARE(old.first().charge, std::optional<double>(64));
        QVERIFY(!old.first().wattsSolar);
        QVERIFY(!old.first().temperature);

        DeviceStatus s = makeSample(kBase.addSecs(60), 65);
        s.wattsSolar = 180;
        s.temperature = 31.5;
        h.addSample(a, s);
        const auto both = h.samples(a, {}, {});
        QCOMPARE(both.size(), 2);
        QCOMPARE(both.last().wattsSolar, std::optional<double>(180));
        QCOMPARE(both.last().temperature, std::optional<double>(31.5));
    }

    void dailyEnergy()
    {
        HistoryStore h;
        QVERIFY(h.open(QStringLiteral(":memory:")));
        // Local days, whatever zone the test runs in.
        const QDate day(2026, 3, 10);
        const QDateTime noon(day, QTime(12, 0));
        const auto sample = [&](const QDateTime& at, double in, double out, double solar = 0) {
            DeviceStatus s = makeSample(at, 80);
            s.wattsIn = in;
            s.wattsOut = out;
            s.wattsSolar = solar;
            h.addSample(a, s);
        };
        // An hour at 600 W in (half of it solar) and 200 W out, a reading a minute.
        for (int m = 0; m <= 60; ++m)
            sample(noon.addSecs(m * 60), 600, 200, 300);
        // Then nothing for two hours (nutty closed): that time counts for nothing.
        sample(noon.addSecs(3 * 3600), 100, 100);
        sample(noon.addSecs(3 * 3600 + 600), 100, 100);  // 10 min: a slow poll still counts
        // The next day, half an hour out.
        for (int m = 0; m <= 30; ++m)
            sample(QDateTime(day.addDays(1), QTime(8, 0)).addSecs(m * 60), 0, 400);
        // Another device doesn't mix in.
        DeviceStatus other = makeSample(noon, 50);
        other.wattsIn = 5000;
        h.addSample(b, other);

        const auto days = h.dailyEnergy(
            a, QDateTime(day.addDays(-1), QTime(0, 0)), QDateTime(day.addDays(1), QTime(23, 0)));
        QCOMPARE(days.size(), 3);
        QCOMPARE(days.at(0).day, day.addDays(-1));
        QCOMPARE(days.at(0).whIn, 0.0);
        QCOMPARE(days.at(1).day, day);
        QCOMPARE(qRound(days.at(1).whIn), 600 + 17);  // + 10 min at 100 W
        QCOMPARE(qRound(days.at(1).whSolar), 300);
        QCOMPARE(qRound(days.at(1).whOut), 200 + 17);
        QCOMPARE(qRound(days.at(2).whOut), 200);
        QCOMPARE(days.at(2).whIn, 0.0);

        QVERIFY(h.dailyEnergy(a, noon, noon.addDays(-1)).isEmpty());
    }

    void schemaVersionRecorded()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("nested/history.db"));
        {
            HistoryStore h;
            QVERIFY(h.open(path));
        }
        const auto name = QStringLiteral("tst-read");
        {
            auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral("SELECT version FROM schema_version")));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), HistoryStore::kSchemaVersion);
            QVERIFY(q.exec(QStringLiteral("PRAGMA journal_mode")));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(), QStringLiteral("wal"));
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
    }

    void csv()
    {
        auto e = makeEvent(EventType::Alarm, a, kBase, QStringLiteral("Alarm"),
            QStringLiteral("UPS-A raised an alarm: \"hot\", very"));
        e.charge = 87.25;
        e.flags = StatusFlags(StatusFlag::Online) | StatusFlag::Alarm;
        const auto text = QString::fromUtf8(HistoryStore::eventsToCsv({ e }));
        const auto lines = text.split(QStringLiteral("\r\n"), Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 2);
        QVERIFY(lines.first().startsWith(QStringLiteral("timestamp,device,type")));
        QVERIFY(lines.at(1).contains(QStringLiteral("\"UPS-A raised an alarm: \"\"hot\"\", very\"")));
        QVERIFY(
            lines.at(1).contains(QStringLiteral(",87.3,")) || lines.at(1).contains(QStringLiteral(",87.2,")));
        QVERIFY(lines.at(1).contains(QStringLiteral("OL ALARM")));

        Episode ep;
        ep.start = kBase;
        ep.end = kBase.addSecs(125);
        ep.deviceName = QStringLiteral("UPS-A");
        ep.actions = { { kBase.addSecs(5), QStringLiteral("Low battery") } };
        const auto epText = QString::fromUtf8(HistoryStore::episodesToCsv({ ep }));
        QVERIFY(epText.contains(QStringLiteral(",125,UPS-A,")));
        QVERIFY(epText.contains(QStringLiteral("Low battery")));

        QTemporaryDir dir;
        HistoryStore h;
        QVERIFY(h.open(QStringLiteral(":memory:")));
        h.addEvent(e);
        h.addEvent(makeEvent(EventType::Test, a, kBase.addSecs(-60), QStringLiteral("first")));
        const auto path = dir.filePath(QStringLiteral("out/events.csv"));
        EventQuery q;
        q.limit = 1;  // ignored by exports
        QVERIFY(h.exportEventsCsv(path, q));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const auto exported
            = QString::fromUtf8(f.readAll()).split(QStringLiteral("\r\n"), Qt::SkipEmptyParts);
        QCOMPARE(exported.size(), 3);
        QVERIFY(exported.at(1).contains(QStringLiteral("first")));  // oldest first
        QVERIFY(h.exportEpisodesCsv(dir.filePath(QStringLiteral("episodes.csv")), {}, {}, {}));
    }
};

QTEST_GUILESS_MAIN(TestHistoryStore)
#include "tst_historystore.moc"
