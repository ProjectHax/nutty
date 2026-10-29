// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/DeviceStatus.h"
#include "core/model/Event.h"

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QUuid>

#include <optional>

namespace nutty {

// Filter for HistoryStore::events()/countEvents(). Empty lists/sets and
// invalid dates mean "no restriction".
struct EventQuery {
    QList<QUuid> devices;
    QSet<EventType> types;
    QDateTime from;
    QDateTime to;
    QString search;  // substring of title or message
    int limit = 200;
    int offset = 0;
    bool newestFirst = true;
};

struct EpisodeAction {
    QDateTime timestamp;
    QString text;
};

// One on-battery episode ("outage") of one device.
struct Episode {
    qint64 id = 0;
    QUuid deviceId;
    QString deviceName;
    QDateTime start;
    QDateTime end;  // invalid while ongoing
    std::optional<double> startCharge;
    std::optional<double> minCharge;
    std::optional<qint64> minRuntime;
    QList<EpisodeAction> actions;

    bool ongoing() const { return !end.isValid(); }
    // Seconds on battery; ongoing episodes are measured up to `now`.
    qint64 durationSec(const QDateTime& now = QDateTime::currentDateTimeUtc()) const;
};

// A (possibly bucket-averaged) chart point.
struct Sample {
    QDateTime timestamp;
    std::optional<double> charge;
    std::optional<double> runtime;  // seconds
    std::optional<double> load;
    std::optional<double> wattsIn;
    std::optional<double> wattsOut;
    std::optional<double> inputVoltage;
    std::optional<double> wattsSolar;
    std::optional<double> temperature;  // °C
    bool onBattery = false;  // any sample in the bucket was on battery
};

// The energy that went in and out of a device on one (local) day.
struct EnergyDay {
    QDate day;
    double whIn = 0;
    double whOut = 0;
    double whSolar = 0;
};

// The history database (SQLite via QtSql): events, on-battery episodes and
// periodic samples for the charts. Timestamps are stored as UTC milliseconds.
// Samples are buffered and written in batches; queries flush first.
class HistoryStore : public QObject
{
    Q_OBJECT

public:
    static constexpr int kSchemaVersion = 2;
    // Two samples further apart than this mean nutty wasn't watching in
    // between, so the time counts for no energy. Longer than a slow poll.
    static constexpr qint64 kMaxEnergyGapMs = 15 * 60 * 1000;

    explicit HistoryStore(QObject* parent = nullptr);
    ~HistoryStore() override;

    // path may be ":memory:". Fails (with lastError()) for a database written
    // by a newer schema.
    bool open(const QString& path);
    void close();
    bool isOpen() const;
    QString lastError() const { return m_lastError; }

    // --- events ---
    // Returns the new row id (0 on failure) and emits eventAdded with the id set.
    qint64 addEvent(const Event& event);
    QList<Event> events(const EventQuery& query) const;
    int countEvents(const EventQuery& query) const;

    // --- episodes ---
    qint64 openEpisode(const QUuid& device, const QString& deviceName, const QDateTime& start,
        std::optional<double> startCharge, std::optional<qint64> startRuntime);
    // Lowers the stored minimums (never raises them).
    void updateEpisode(qint64 id, std::optional<double> charge, std::optional<qint64> runtime);
    void appendEpisodeAction(qint64 id, const QDateTime& when, const QString& text);
    void closeEpisode(qint64 id, const QDateTime& end);
    // Closes episodes left open by a crash or shutdown, ending each at its
    // device's last sample (or its start when there is none).
    void closeOpenEpisodes();
    std::optional<Episode> episode(qint64 id) const;
    // Episodes overlapping [from, to]; null device = all devices. Newest first.
    QList<Episode> episodes(const QUuid& device, const QDateTime& from, const QDateTime& to, int limit = 200,
        int offset = 0) const;
    int countEpisodes(const QUuid& device, const QDateTime& from, const QDateTime& to) const;

    // --- samples ---
    void addSample(const QUuid& device, const DeviceStatus& status);
    void flush();
    // Samples in [from, to], oldest first. With maxPoints > 0 the range is
    // split into at most that many buckets and each bucket is averaged.
    QList<Sample> samples(
        const QUuid& device, const QDateTime& from, const QDateTime& to, int maxPoints = 0) const;
    // Wh in, out and from solar for every local day from `from` to `to`,
    // oldest first, worked out from the samples' power readings.
    QList<EnergyDay> dailyEnergy(const QUuid& device, const QDateTime& from, const QDateTime& to) const;

    // --- maintenance ---
    // Deletes samples older than sampleDays and events/closed episodes older
    // than eventDays (<= 0 keeps forever).
    void prune(int sampleDays, int eventDays, const QDateTime& now = QDateTime::currentDateTimeUtc());
    // Deletes everything recorded for a device (e.g. when it is removed).
    void forgetDevice(const QUuid& device);

    // CSV with a header row; timestamps in local time, ISO 8601.
    static QByteArray eventsToCsv(const QList<Event>& events);
    static QByteArray episodesToCsv(const QList<Episode>& episodes);
    bool exportEventsCsv(const QString& path, EventQuery query) const;
    bool exportEpisodesCsv(
        const QString& path, const QUuid& device, const QDateTime& from, const QDateTime& to) const;

signals:
    void eventAdded(const nutty::Event& event);
    void episodeChanged(qint64 id);

private:
    struct PendingSample {
        QUuid device;
        qint64 ts;
        DeviceStatus status;
    };

    bool exec(const QString& sql) const;
    bool migrate();
    void flushPending() const;
    void setError(const QString& error) const;

    QString m_connection;
    mutable QString m_lastError;
    mutable QList<PendingSample> m_pending;
    QTimer m_flushTimer;
};

}  // namespace nutty
