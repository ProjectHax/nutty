// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/storage/HistoryStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimeZone>

#include <algorithm>
#include <limits>

namespace nutty {

Q_LOGGING_CATEGORY(lcHistory, "nutty.history")

namespace {

constexpr int kFlushRows = 100;
constexpr int kFlushIntervalMs = 10'000;
constexpr qint64 kDayMs = 24LL * 60 * 60 * 1000;

QString idText(const QUuid& id)
{
    return id.toString(QUuid::WithoutBraces);
}

qint64 toMs(const QDateTime& dt)
{
    return dt.toMSecsSinceEpoch();
}

QDateTime fromMs(const QVariant& v)
{
    if (v.isNull())
        return {};
    return QDateTime::fromMSecsSinceEpoch(v.toLongLong(), QTimeZone::utc());
}

template <typename T> QVariant opt(const std::optional<T>& value)
{
    return value ? QVariant::fromValue(*value) : QVariant();
}

std::optional<double> optDouble(const QVariant& v)
{
    if (v.isNull())
        return std::nullopt;
    return v.toDouble();
}

std::optional<qint64> optInt(const QVariant& v)
{
    if (v.isNull())
        return std::nullopt;
    return v.toLongLong();
}

QString jsonText(const QVariantMap& map)
{
    if (map.isEmpty())
        return {};
    return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(map)).toJson(QJsonDocument::Compact));
}

QList<EpisodeAction> parseActions(const QString& json)
{
    QList<EpisodeAction> out;
    const auto array = QJsonDocument::fromJson(json.toUtf8()).array();
    for (const auto& value : array) {
        const auto o = value.toObject();
        out.append({ fromMs(o.value(QStringLiteral("ts")).toVariant()),
            o.value(QStringLiteral("text")).toString() });
    }
    return out;
}

QString actionsJson(const QList<EpisodeAction>& actions)
{
    QJsonArray array;
    for (const auto& a : actions)
        array.append(
            QJsonObject { { QStringLiteral("ts"), toMs(a.timestamp) }, { QStringLiteral("text"), a.text } });
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

// The initial schema (version 1).
const char* const kSchemaV1[] = {
    "CREATE TABLE events ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " ts INTEGER NOT NULL,"
    " device_id TEXT,"
    " device_name TEXT,"
    " type TEXT NOT NULL,"
    " severity TEXT NOT NULL,"
    " title TEXT,"
    " message TEXT,"
    " flags INTEGER,"
    " charge REAL,"
    " runtime INTEGER,"
    " load REAL,"
    " extra TEXT)",
    "CREATE INDEX idx_events_ts ON events(ts)",
    "CREATE INDEX idx_events_device_ts ON events(device_id, ts)",
    "CREATE TABLE episodes ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " device_id TEXT NOT NULL,"
    " device_name TEXT,"
    " start_ts INTEGER NOT NULL,"
    " end_ts INTEGER,"
    " start_charge REAL,"
    " min_charge REAL,"
    " min_runtime INTEGER,"
    " actions TEXT)",
    "CREATE INDEX idx_episodes_device_start ON episodes(device_id, start_ts)",
    "CREATE INDEX idx_episodes_start ON episodes(start_ts)",
    "CREATE TABLE samples ("
    " device_id TEXT NOT NULL,"
    " ts INTEGER NOT NULL,"
    " charge REAL,"
    " runtime INTEGER,"
    " load REAL,"
    " watts_in REAL,"
    " watts_out REAL,"
    " input_voltage REAL,"
    " flags INTEGER,"
    " PRIMARY KEY (device_id, ts)) WITHOUT ROWID",
};

// Version 2: solar power and temperature in the samples.
const char* const kSchemaV2[] = {
    "ALTER TABLE samples ADD COLUMN watts_solar REAL",
    "ALTER TABLE samples ADD COLUMN temperature REAL",
};

QString csvField(const QString& value)
{
    if (value.contains(u',') || value.contains(u'"') || value.contains(u'\n') || value.contains(u'\r')) {
        QString quoted = value;
        quoted.replace(u'"', QStringLiteral("\"\""));
        return u'"' + quoted + u'"';
    }
    return value;
}

QString csvNumber(const std::optional<double>& v)
{
    return v ? QString::number(*v, 'f', 1) : QString();
}

QString csvNumber(const std::optional<qint64>& v)
{
    return v ? QString::number(*v) : QString();
}

QString localIso(const QDateTime& dt)
{
    return dt.isValid() ? dt.toLocalTime().toString(Qt::ISODate) : QString();
}

QByteArray csvLine(const QStringList& fields)
{
    QStringList escaped;
    escaped.reserve(fields.size());
    for (const auto& f : fields)
        escaped << csvField(f);
    return escaped.join(u',').toUtf8() + "\r\n";
}

bool writeFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(data);
    return file.commit();
}

}  // namespace

qint64 Episode::durationSec(const QDateTime& now) const
{
    if (!start.isValid())
        return 0;
    return std::max<qint64>(0, start.secsTo(end.isValid() ? end : now));
}

HistoryStore::HistoryStore(QObject* parent)
    : QObject(parent)
{
    m_flushTimer.setSingleShot(true);
    m_flushTimer.setInterval(kFlushIntervalMs);
    connect(&m_flushTimer, &QTimer::timeout, this, &HistoryStore::flush);
}

HistoryStore::~HistoryStore()
{
    close();
}

void HistoryStore::setError(const QString& error) const
{
    m_lastError = error;
    qCWarning(lcHistory).noquote() << error;
}

bool HistoryStore::isOpen() const
{
    return !m_connection.isEmpty() && QSqlDatabase::database(m_connection, false).isOpen();
}

bool HistoryStore::open(const QString& path)
{
    close();
    m_lastError.clear();
    m_connection = QStringLiteral("nutty-history-%1").arg(idText(QUuid::createUuid()));
    {
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
        if (path != QLatin1String(":memory:"))
            QDir().mkpath(QFileInfo(path).absolutePath());
        db.setDatabaseName(path);
        if (!db.open()) {
            setError(db.lastError().text());
            db = {};
            QSqlDatabase::removeDatabase(m_connection);
            m_connection.clear();
            return false;
        }
    }
    if (path != QLatin1String(":memory:")) {
        exec(QStringLiteral("PRAGMA journal_mode=WAL"));
        exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    }
    if (!migrate()) {
        const auto error = m_lastError;
        close();
        m_lastError = error;
        return false;
    }
    return true;
}

void HistoryStore::close()
{
    if (m_connection.isEmpty())
        return;
    m_flushTimer.stop();
    flushPending();
    {
        auto db = QSqlDatabase::database(m_connection, false);
        if (db.isOpen())
            db.close();
    }
    QSqlDatabase::removeDatabase(m_connection);
    m_connection.clear();
}

bool HistoryStore::exec(const QString& sql) const
{
    QSqlQuery q(QSqlDatabase::database(m_connection, false));
    if (!q.exec(sql)) {
        setError(q.lastError().text());
        return false;
    }
    return true;
}

bool HistoryStore::migrate()
{
    auto db = QSqlDatabase::database(m_connection, false);
    if (!exec(QStringLiteral("CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL)")))
        return false;

    int version = 0;
    {
        QSqlQuery q(db);
        if (q.exec(QStringLiteral("SELECT version FROM schema_version LIMIT 1")) && q.next())
            version = q.value(0).toInt();
    }
    if (version > kSchemaVersion) {
        setError(QCoreApplication::translate(
            "nutty::Storage", "The history database was created by a newer version of nutty."));
        return false;
    }
    if (version == kSchemaVersion)
        return true;

    db.transaction();
    bool ok = true;
    if (version < 1) {
        for (const char* sql : kSchemaV1)
            ok = ok && exec(QString::fromLatin1(sql));
    }
    if (version < 2) {
        for (const char* sql : kSchemaV2)
            ok = ok && exec(QString::fromLatin1(sql));
    }
    // Future migrations: if (version < 3) { ... }
    ok = ok && exec(QStringLiteral("DELETE FROM schema_version"));
    if (ok) {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO schema_version (version) VALUES (?)"));
        q.addBindValue(kSchemaVersion);
        ok = q.exec();
        if (!ok)
            setError(q.lastError().text());
    }
    if (ok)
        db.commit();
    else
        db.rollback();
    return ok;
}

// --- events -------------------------------------------------------------------

qint64 HistoryStore::addEvent(const Event& event)
{
    if (!isOpen())
        return 0;
    QSqlQuery q(QSqlDatabase::database(m_connection, false));
    q.prepare(QStringLiteral("INSERT INTO events (ts, device_id, device_name, type, severity, title, "
                             "message, flags, charge, runtime, "
                             "load, extra) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    const auto ts = event.timestamp.isValid() ? event.timestamp : QDateTime::currentDateTimeUtc();
    q.addBindValue(toMs(ts));
    q.addBindValue(event.deviceId.isNull() ? QVariant() : QVariant(idText(event.deviceId)));
    q.addBindValue(event.deviceName);
    q.addBindValue(enumKey(event.type));
    q.addBindValue(enumKey(event.severity));
    q.addBindValue(event.title);
    q.addBindValue(event.message);
    q.addBindValue(static_cast<qint64>(event.flags.toInt()));
    q.addBindValue(opt(event.charge));
    q.addBindValue(opt(event.runtime));
    q.addBindValue(opt(event.load));
    q.addBindValue(jsonText(event.extra));
    if (!q.exec()) {
        setError(q.lastError().text());
        return 0;
    }
    Event stored = event;
    stored.id = q.lastInsertId().toLongLong();
    stored.timestamp = ts;
    emit eventAdded(stored);
    return stored.id;
}

namespace {

// Builds "WHERE ..." for an EventQuery and the values to bind, in order.
QString eventWhere(const EventQuery& query, QVariantList& binds)
{
    QStringList clauses;
    if (!query.devices.isEmpty()) {
        QStringList marks;
        for (const auto& id : query.devices) {
            marks << QStringLiteral("?");
            binds << idText(id);
        }
        clauses << QStringLiteral("device_id IN (%1)").arg(marks.join(u','));
    }
    if (!query.types.isEmpty()) {
        QStringList marks;
        for (const auto type : query.types) {
            marks << QStringLiteral("?");
            binds << enumKey(type);
        }
        clauses << QStringLiteral("type IN (%1)").arg(marks.join(u','));
    }
    if (query.from.isValid()) {
        clauses << QStringLiteral("ts >= ?");
        binds << toMs(query.from);
    }
    if (query.to.isValid()) {
        clauses << QStringLiteral("ts <= ?");
        binds << toMs(query.to);
    }
    if (!query.search.trimmed().isEmpty()) {
        QString term = query.search.trimmed();
        term.replace(u'\\', QStringLiteral("\\\\"))
            .replace(u'%', QStringLiteral("\\%"))
            .replace(u'_', QStringLiteral("\\_"));
        term = u'%' + term + u'%';
        clauses << QStringLiteral(
            "(title LIKE ? ESCAPE '\\' OR message LIKE ? ESCAPE '\\' OR device_name LIKE ? ESCAPE '\\')");
        binds << term << term << term;
    }
    return clauses.isEmpty() ? QString() : QStringLiteral(" WHERE ") + clauses.join(QStringLiteral(" AND "));
}

Event eventFromRow(const QSqlQuery& q)
{
    Event e;
    e.id = q.value(0).toLongLong();
    e.timestamp = fromMs(q.value(1));
    e.deviceId = QUuid(q.value(2).toString());
    e.deviceName = q.value(3).toString();
    e.type = enumFromKey(q.value(4).toString(), EventType::Test);
    e.severity = enumFromKey(q.value(5).toString(), Severity::Info);
    e.title = q.value(6).toString();
    e.message = q.value(7).toString();
    e.flags = StatusFlags::fromInt(static_cast<StatusFlags::Int>(q.value(8).toLongLong()));
    e.charge = optDouble(q.value(9));
    e.runtime = optInt(q.value(10));
    e.load = optDouble(q.value(11));
    const auto extra = q.value(12).toString();
    if (!extra.isEmpty())
        e.extra = QJsonDocument::fromJson(extra.toUtf8()).object().toVariantMap();
    return e;
}

}  // namespace

QList<Event> HistoryStore::events(const EventQuery& query) const
{
    QList<Event> out;
    if (!isOpen())
        return out;
    QVariantList binds;
    const auto where = eventWhere(query, binds);
    QSqlQuery q(QSqlDatabase::database(m_connection, false));
    q.prepare(QStringLiteral(
        "SELECT id, ts, device_id, device_name, type, severity, title, message, flags, charge, "
        "runtime, load, extra FROM events%1 ORDER BY ts %2, id %2 LIMIT ? OFFSET ?")
            .arg(where, query.newestFirst ? QStringLiteral("DESC") : QStringLiteral("ASC")));
    for (const auto& b : std::as_const(binds))
        q.addBindValue(b);
    q.addBindValue(query.limit > 0 ? query.limit : -1);
    q.addBindValue(std::max(0, query.offset));
    if (!q.exec()) {
        setError(q.lastError().text());
        return out;
    }
    while (q.next())
        out << eventFromRow(q);
    return out;
}

int HistoryStore::countEvents(const EventQuery& query) const
{
    if (!isOpen())
        return 0;
    QVariantList binds;
    const auto where = eventWhere(query, binds);
    QSqlQuery q(QSqlDatabase::database(m_connection, false));
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM events%1").arg(where));
    for (const auto& b : std::as_const(binds))
        q.addBindValue(b);
    if (!q.exec() || !q.next()) {
        setError(q.lastError().text());
        return 0;
    }
    return q.value(0).toInt();
}

// --- episodes -------------------------------------------------------------------

qint64 HistoryStore::openEpisode(const QUuid& device, const QString& deviceName, const QDateTime& start,
    std::optional<double> startCharge, std::optional<qint64> startRuntime)
{
    if (!isOpen())
        return 0;
    QSqlQuery q(QSqlDatabase::database(m_connection, false));
    q.prepare(
        QStringLiteral("INSERT INTO episodes (device_id, device_name, start_ts, start_charge, min_charge, "
                       "min_runtime, actions) VALUES (?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(idText(device));
    q.addBindValue(deviceName);
    q.addBindValue(toMs(start));
    q.addBindValue(opt(startCharge));
    q.addBindValue(opt(startCharge));
    q.addBindValue(opt(startRuntime));
    q.addBindValue(QStringLiteral("[]"));
    if (!q.exec()) {
        setError(q.lastError().text());
        return 0;
    }
    const auto id = q.lastInsertId().toLongLong();
    emit episodeChanged(id);
    return id;
}

void HistoryStore::updateEpisode(qint64 id, std::optional<double> charge, std::optional<qint64> runtime)
{
    if (!isOpen() || id <= 0 || (!charge && !runtime))
        return;
    const auto current = episode(id);
    if (!current)
        return;
    auto minCharge = current->minCharge;
    auto minRuntime = current->minRuntime;
    bool changed = false;
    if (charge && (!minCharge || *charge < *minCharge)) {
        minCharge = charge;
        changed = true;
    }
    if (runtime && (!minRuntime || *runtime < *minRuntime)) {
        minRuntime = runtime;
        changed = true;
    }
    if (!changed)
        return;
    QSqlQuery q(QSqlDatabase::database(m_connection, false));
    q.prepare(QStringLiteral("UPDATE episodes SET min_charge = ?, min_runtime = ? WHERE id = ?"));
    q.addBindValue(opt(minCharge));
    q.addBindValue(opt(minRuntime));
    q.addBindValue(id);
    if (!q.exec()) {
        setError(q.lastError().text());
        return;
    }
    emit episodeChanged(id);
}

void HistoryStore::appendEpisodeAction(qint64 id, const QDateTime& when, const QString& text)
{
    if (!isOpen() || id <= 0)
        return;
    auto current = episode(id);
    if (!current)
        return;
    current->actions.append({ when, text });
    QSqlQuery q(QSqlDatabase::database(m_connection, false));
    q.prepare(QStringLiteral("UPDATE episodes SET actions = ? WHERE id = ?"));
    q.addBindValue(actionsJson(current->actions));
    q.addBindValue(id);
    if (!q.exec()) {
        setError(q.lastError().text());
        return;
    }
    emit episodeChanged(id);
}

void HistoryStore::closeEpisode(qint64 id, const QDateTime& end)
{
    if (!isOpen() || id <= 0)
        return;
    QSqlQuery q(QSqlDatabase::database(m_connection, false));
    q.prepare(QStringLiteral("UPDATE episodes SET end_ts = ? WHERE id = ? AND end_ts IS NULL"));
    q.addBindValue(toMs(end));
    q.addBindValue(id);
    if (!q.exec()) {
        setError(q.lastError().text());
        return;
    }
    if (q.numRowsAffected() > 0)
        emit episodeChanged(id);
}

void HistoryStore::closeOpenEpisodes()
{
    if (!isOpen())
        return;
    flushPending();
    exec(QStringLiteral(
        "UPDATE episodes SET end_ts = COALESCE((SELECT MAX(s.ts) FROM samples s WHERE s.device_id = "
        "episodes.device_id AND s.ts >= episodes.start_ts), episodes.start_ts) WHERE end_ts IS NULL"));
}

namespace {

constexpr auto kEpisodeColumns = "SELECT id, device_id, device_name, start_ts, end_ts, start_charge, "
                                 "min_charge, min_runtime, actions FROM episodes";

Episode episodeFromRow(const QSqlQuery& q)
{
    Episode e;
    e.id = q.value(0).toLongLong();
    e.deviceId = QUuid(q.value(1).toString());
    e.deviceName = q.value(2).toString();
    e.start = fromMs(q.value(3));
    e.end = fromMs(q.value(4));
    e.startCharge = optDouble(q.value(5));
    e.minCharge = optDouble(q.value(6));
    e.minRuntime = optInt(q.value(7));
    e.actions = parseActions(q.value(8).toString());
    return e;
}

QString episodeWhere(const QUuid& device, const QDateTime& from, const QDateTime& to, QVariantList& binds)
{
    QStringList clauses;
    if (!device.isNull()) {
        clauses << QStringLiteral("device_id = ?");
        binds << idText(device);
    }
    if (to.isValid()) {
        clauses << QStringLiteral("start_ts <= ?");
        binds << toMs(to);
    }
    if (from.isValid()) {
        clauses << QStringLiteral("(end_ts IS NULL OR end_ts >= ?)");
        binds << toMs(from);
    }
    return clauses.isEmpty() ? QString() : QStringLiteral(" WHERE ") + clauses.join(QStringLiteral(" AND "));
}

}  // namespace

std::optional<Episode> HistoryStore::episode(qint64 id) const
{
    if (!isOpen())
        return std::nullopt;
    QSqlQuery q(QSqlDatabase::database(m_connection, false));
    q.prepare(QString::fromLatin1(kEpisodeColumns) + QStringLiteral(" WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec() || !q.next())
        return std::nullopt;
    return episodeFromRow(q);
}

QList<Episode> HistoryStore::episodes(
    const QUuid& device, const QDateTime& from, const QDateTime& to, int limit, int offset) const
{
    QList<Episode> out;
    if (!isOpen())
        return out;
    QVariantList binds;
    const auto where = episodeWhere(device, from, to, binds);
    QSqlQuery q(QSqlDatabase::database(m_connection, false));
    q.prepare(QString::fromLatin1(kEpisodeColumns) + where
        + QStringLiteral(" ORDER BY start_ts DESC, id DESC LIMIT ? OFFSET ?"));
    for (const auto& b : std::as_const(binds))
        q.addBindValue(b);
    q.addBindValue(limit > 0 ? limit : -1);
    q.addBindValue(std::max(0, offset));
    if (!q.exec()) {
        setError(q.lastError().text());
        return out;
    }
    while (q.next())
        out << episodeFromRow(q);
    return out;
}

int HistoryStore::countEpisodes(const QUuid& device, const QDateTime& from, const QDateTime& to) const
{
    if (!isOpen())
        return 0;
    QVariantList binds;
    const auto where = episodeWhere(device, from, to, binds);
    QSqlQuery q(QSqlDatabase::database(m_connection, false));
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM episodes") + where);
    for (const auto& b : std::as_const(binds))
        q.addBindValue(b);
    if (!q.exec() || !q.next())
        return 0;
    return q.value(0).toInt();
}

// --- samples ----------------------------------------------------------------------

void HistoryStore::addSample(const QUuid& device, const DeviceStatus& status)
{
    if (!isOpen())
        return;
    const auto ts = status.sampledAt.isValid() ? status.sampledAt : QDateTime::currentDateTimeUtc();
    m_pending.append({ device, toMs(ts), status });
    if (m_pending.size() >= kFlushRows)
        flush();
    else if (!m_flushTimer.isActive())
        m_flushTimer.start();
}

void HistoryStore::flush()
{
    m_flushTimer.stop();
    flushPending();
}

void HistoryStore::flushPending() const
{
    if (m_pending.isEmpty() || m_connection.isEmpty())
        return;
    auto db = QSqlDatabase::database(m_connection, false);
    if (!db.isOpen())
        return;
    db.transaction();
    QSqlQuery q(db);
    q.prepare(
        QStringLiteral("INSERT OR REPLACE INTO samples (device_id, ts, charge, runtime, load, watts_in, "
                       "watts_out, input_voltage, flags, watts_solar, temperature) "
                       "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    for (const auto& p : std::as_const(m_pending)) {
        q.addBindValue(idText(p.device));
        q.addBindValue(p.ts);
        q.addBindValue(opt(p.status.charge));
        q.addBindValue(opt(p.status.runtime));
        q.addBindValue(opt(p.status.load));
        q.addBindValue(opt(p.status.wattsIn));
        q.addBindValue(opt(p.status.wattsOut));
        q.addBindValue(opt(p.status.inputVoltage));
        q.addBindValue(static_cast<qint64>(p.status.flags.toInt()));
        q.addBindValue(opt(p.status.wattsSolar));
        q.addBindValue(opt(p.status.temperature));
        if (!q.exec())
            setError(q.lastError().text());
    }
    db.commit();
    m_pending.clear();
}

QList<Sample> HistoryStore::samples(
    const QUuid& device, const QDateTime& from, const QDateTime& to, int maxPoints) const
{
    QList<Sample> out;
    if (!isOpen())
        return out;
    flushPending();

    QSqlQuery q(QSqlDatabase::database(m_connection, false));
    const bool bucketed = maxPoints > 0 && from.isValid() && to.isValid() && from < to;
    const qint64 fromMsValue = from.isValid() ? toMs(from) : std::numeric_limits<qint64>::min();
    const qint64 toMsValue = to.isValid() ? toMs(to) : std::numeric_limits<qint64>::max();
    if (bucketed) {
        // +1 so a sample exactly at `to` still lands in the last bucket.
        const qint64 bucket = (toMsValue - fromMsValue) / maxPoints + 1;
        q.prepare(QStringLiteral("SELECT CAST(AVG(ts) AS INTEGER), AVG(charge), AVG(runtime), AVG(load), "
                                 "AVG(watts_in), AVG(watts_out), "
                                 "AVG(input_voltage), MAX((flags & %1) != 0), AVG(watts_solar), "
                                 "AVG(temperature) FROM samples WHERE device_id = "
                                 "? AND ts >= ? AND ts <= ? "
                                 "GROUP BY (ts - ?) / ? ORDER BY 1")
                .arg(static_cast<int>(StatusFlag::OnBattery)));
        q.addBindValue(idText(device));
        q.addBindValue(fromMsValue);
        q.addBindValue(toMsValue);
        q.addBindValue(fromMsValue);
        q.addBindValue(bucket);
    } else {
        q.prepare(QStringLiteral("SELECT ts, charge, runtime, load, watts_in, watts_out, input_voltage, "
                                 "(flags & %1) != 0, watts_solar, temperature FROM samples "
                                 "WHERE device_id = ? AND ts >= ? AND ts <= ? ORDER BY ts")
                .arg(static_cast<int>(StatusFlag::OnBattery)));
        q.addBindValue(idText(device));
        q.addBindValue(fromMsValue);
        q.addBindValue(toMsValue);
    }
    if (!q.exec()) {
        setError(q.lastError().text());
        return out;
    }
    while (q.next()) {
        Sample s;
        s.timestamp = fromMs(q.value(0));
        s.charge = optDouble(q.value(1));
        s.runtime = optDouble(q.value(2));
        s.load = optDouble(q.value(3));
        s.wattsIn = optDouble(q.value(4));
        s.wattsOut = optDouble(q.value(5));
        s.inputVoltage = optDouble(q.value(6));
        s.onBattery = q.value(7).toInt() != 0;
        s.wattsSolar = optDouble(q.value(8));
        s.temperature = optDouble(q.value(9));
        out << s;
    }
    return out;
}

QList<EnergyDay> HistoryStore::dailyEnergy(
    const QUuid& device, const QDateTime& from, const QDateTime& to) const
{
    QList<EnergyDay> days;
    if (!isOpen() || !from.isValid() || !to.isValid() || to < from)
        return days;
    flushPending();

    // Where each local day starts, plus where the last one ends.
    QList<qint64> starts;
    for (QDate d = from.toLocalTime().date(); d <= to.toLocalTime().date(); d = d.addDays(1)) {
        days.append({ d });
        starts << d.startOfDay().toMSecsSinceEpoch();
    }
    starts << days.last().day.addDays(1).startOfDay().toMSecsSinceEpoch();

    QSqlQuery q(QSqlDatabase::database(m_connection, false));
    q.setForwardOnly(true);
    q.prepare(QStringLiteral("SELECT ts, watts_in, watts_out, watts_solar FROM samples "
                             "WHERE device_id = ? AND ts >= ? AND ts < ? ORDER BY ts"));
    q.addBindValue(idText(device));
    q.addBindValue(starts.first());
    q.addBindValue(starts.last());
    if (!q.exec()) {
        setError(q.lastError().text());
        return days;
    }
    // Each reading holds until the next one.
    qint64 previous = 0;
    double in = 0, out = 0, solar = 0;
    qsizetype day = 0;
    bool first = true;
    while (q.next()) {
        const qint64 ts = q.value(0).toLongLong();
        if (!first && ts - previous <= kMaxEnergyGapMs) {
            while (day + 1 < days.size() && previous >= starts[day + 1])
                ++day;
            const double hours = static_cast<double>(ts - previous) / 3'600'000.0;
            days[day].whIn += in * hours;
            days[day].whOut += out * hours;
            days[day].whSolar += solar * hours;
        }
        first = false;
        previous = ts;
        in = std::max(0.0, q.value(1).toDouble());
        out = std::max(0.0, q.value(2).toDouble());
        solar = std::max(0.0, q.value(3).toDouble());
    }
    return days;
}

// --- maintenance ------------------------------------------------------------------

void HistoryStore::prune(int sampleDays, int eventDays, const QDateTime& now)
{
    if (!isOpen())
        return;
    flushPending();
    const qint64 nowMs = toMs(now);
    auto db = QSqlDatabase::database(m_connection, false);
    if (sampleDays > 0) {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("DELETE FROM samples WHERE ts < ?"));
        q.addBindValue(nowMs - sampleDays * kDayMs);
        if (!q.exec())
            setError(q.lastError().text());
    }
    if (eventDays > 0) {
        const qint64 cutoff = nowMs - eventDays * kDayMs;
        QSqlQuery q(db);
        q.prepare(QStringLiteral("DELETE FROM events WHERE ts < ?"));
        q.addBindValue(cutoff);
        if (!q.exec())
            setError(q.lastError().text());
        QSqlQuery e(db);
        e.prepare(QStringLiteral("DELETE FROM episodes WHERE end_ts IS NOT NULL AND end_ts < ?"));
        e.addBindValue(cutoff);
        if (!e.exec())
            setError(e.lastError().text());
    }
}

void HistoryStore::forgetDevice(const QUuid& device)
{
    if (!isOpen())
        return;
    flushPending();
    auto db = QSqlDatabase::database(m_connection, false);
    for (const auto* table : { "events", "episodes", "samples" }) {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("DELETE FROM %1 WHERE device_id = ?").arg(QLatin1StringView(table)));
        q.addBindValue(idText(device));
        if (!q.exec())
            setError(q.lastError().text());
    }
}

QByteArray HistoryStore::eventsToCsv(const QList<Event>& events)
{
    QByteArray out = csvLine({ QStringLiteral("timestamp"), QStringLiteral("device"), QStringLiteral("type"),
        QStringLiteral("severity"), QStringLiteral("title"), QStringLiteral("message"),
        QStringLiteral("charge_percent"), QStringLiteral("runtime_seconds"), QStringLiteral("load_percent"),
        QStringLiteral("status") });
    for (const auto& e : events) {
        out += csvLine({ localIso(e.timestamp), e.deviceName, enumKey(e.type), enumKey(e.severity), e.title,
            e.message, csvNumber(e.charge), csvNumber(e.runtime), csvNumber(e.load), statusTokens(e.flags) });
    }
    return out;
}

QByteArray HistoryStore::episodesToCsv(const QList<Episode>& episodes)
{
    QByteArray out = csvLine({ QStringLiteral("start"), QStringLiteral("end"),
        QStringLiteral("duration_seconds"), QStringLiteral("device"), QStringLiteral("start_charge_percent"),
        QStringLiteral("min_charge_percent"), QStringLiteral("min_runtime_seconds"),
        QStringLiteral("actions") });
    for (const auto& e : episodes) {
        QStringList actions;
        for (const auto& a : e.actions)
            actions << QStringLiteral("%1 %2").arg(localIso(a.timestamp), a.text);
        out += csvLine({ localIso(e.start), localIso(e.end), QString::number(e.durationSec()), e.deviceName,
            csvNumber(e.startCharge), csvNumber(e.minCharge), csvNumber(e.minRuntime),
            actions.join(QStringLiteral("; ")) });
    }
    return out;
}

bool HistoryStore::exportEventsCsv(const QString& path, EventQuery query) const
{
    query.limit = 0;
    query.offset = 0;
    query.newestFirst = false;
    return writeFile(path, eventsToCsv(events(query)));
}

bool HistoryStore::exportEpisodesCsv(
    const QString& path, const QUuid& device, const QDateTime& from, const QDateTime& to) const
{
    auto list = episodes(device, from, to, 0, 0);
    std::ranges::reverse(list);  // oldest first reads better in a spreadsheet
    return writeFile(path, episodesToCsv(list));
}

}  // namespace nutty
