// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "app/viewmodels/Series.h"

#include "app/viewmodels/AppContext.h"
#include "core/engine/Monitor.h"
#include "core/storage/HistoryStore.h"

#include <algorithm>
#include <limits>

namespace nutty {

namespace {
std::optional<double> pick(const Sample& s, const QString& metric)
{
    if (metric == u"runtime")
        return s.runtime ? std::optional<double>(*s.runtime / 60.0) : std::nullopt;
    if (metric == u"load")
        return s.load;
    if (metric == u"wattsIn")
        return s.wattsIn;
    if (metric == u"wattsOut")
        return s.wattsOut;
    if (metric == u"wattsSolar")
        return s.wattsSolar;
    if (metric == u"inputVoltage")
        return s.inputVoltage;
    if (metric == u"temperature")
        return s.temperature;
    return s.charge;
}
}  // namespace

Series::Series(AppContext* context, QObject* parent)
    : QObject(parent)
    , m_context(context ? context : AppContext::instance())
{
    // Redraw at most twice a minute however fast the devices report.
    m_throttle.setInterval(30 * 1000);
    connect(&m_throttle, &QTimer::timeout, this, [this] {
        if (!m_dirty) {
            m_throttle.stop();
            return;
        }
        m_dirty = false;
        ++m_revision;
        emit revisionChanged();
    });
    if (!m_context)
        return;
    connect(m_context->monitor(), &Monitor::deviceStatusChanged, this, &Series::bump);
    if (HistoryStore* store = m_context->history())
        connect(store, &HistoryStore::episodeChanged, this, &Series::bump);
}

Series* Series::create(QQmlEngine*, QJSEngine*)
{
    return new Series(AppContext::instance());
}

void Series::bump()
{
    m_dirty = true;
    if (!m_throttle.isActive()) {
        // The first change after a quiet spell shows right away.
        m_dirty = false;
        ++m_revision;
        emit revisionChanged();
        m_throttle.start();
    }
}

double Series::now() const
{
    const QDateTime t = m_context ? m_context->monitor()->now() : QDateTime::currentDateTimeUtc();
    return static_cast<double>(t.toMSecsSinceEpoch());
}

QVariantMap Series::energy(const QString& deviceId, int days) const
{
    QVariantList list;
    double in = 0, out = 0, solar = 0, peak = 0;
    HistoryStore* store = m_context ? m_context->history() : nullptr;
    if (store && store->isOpen() && days > 0) {
        const QDateTime now = m_context->monitor()->now();
        const QDateTime from = now.toLocalTime().date().addDays(1 - days).startOfDay();
        for (const EnergyDay& d : store->dailyEnergy(parseId(deviceId), from, now)) {
            list.append(QVariantMap {
                { QStringLiteral("x"), static_cast<double>(d.day.startOfDay().toMSecsSinceEpoch()) },
                { QStringLiteral("whIn"), d.whIn },
                { QStringLiteral("whOut"), d.whOut },
                { QStringLiteral("whSolar"), d.whSolar },
            });
            in += d.whIn;
            out += d.whOut;
            solar += d.whSolar;
            peak = std::max({ peak, d.whIn, d.whOut });
        }
    }
    return {
        { QStringLiteral("days"), list },
        { QStringLiteral("whIn"), in },
        { QStringLiteral("whOut"), out },
        { QStringLiteral("whSolar"), solar },
        { QStringLiteral("max"), peak },
    };
}

QVariantMap Series::get(
    const QString& deviceId, const QString& metric, double from, double to, int maxPoints) const
{
    QVariantList points;
    QVariantList bands;
    double lo = std::numeric_limits<double>::max();
    double hi = std::numeric_limits<double>::lowest();
    QVariant last;
    HistoryStore* store = m_context ? m_context->history() : nullptr;
    if (store && store->isOpen() && to > from) {
        const QUuid device = parseId(deviceId);
        const QDateTime start = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(from)).toUTC();
        const QDateTime end = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(to)).toUTC();
        for (const Sample& s : store->samples(device, start, end, maxPoints)) {
            const auto v = pick(s, metric);
            if (!v)
                continue;
            points.append(QVariantMap {
                { QStringLiteral("x"), static_cast<double>(s.timestamp.toMSecsSinceEpoch()) },
                { QStringLiteral("y"), *v },
            });
            lo = std::min(lo, *v);
            hi = std::max(hi, *v);
            last = *v;
        }
        const QDateTime clock = m_context->monitor()->now();
        for (const Episode& e : store->episodes(device, start, end, 500, 0)) {
            const qint64 b = std::max(e.start.toMSecsSinceEpoch(), start.toMSecsSinceEpoch());
            const qint64 f
                = std::min((e.ongoing() ? clock : e.end).toMSecsSinceEpoch(), end.toMSecsSinceEpoch());
            if (f > b)
                bands.append(QVariantMap { { QStringLiteral("from"), static_cast<double>(b) },
                    { QStringLiteral("to"), static_cast<double>(f) } });
        }
    }
    return {
        { QStringLiteral("points"), points },
        { QStringLiteral("bands"), bands },
        { QStringLiteral("min"), points.isEmpty() ? QVariant() : QVariant(lo) },
        { QStringLiteral("max"), points.isEmpty() ? QVariant() : QVariant(hi) },
        { QStringLiteral("last"), last },
    };
}

}  // namespace nutty
