// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "app/viewmodels/Format.h"

#include <QLocale>

#include <cmath>

namespace nutty {

namespace {

const QString kUnknown = QStringLiteral("—");

// The locale's short time format without seconds (some locales, such as C,
// include them).
QString shortTimeFormat()
{
    QString format = QLocale().timeFormat(QLocale::ShortFormat);
    format.remove(QStringLiteral(":ss"));
    format.remove(QStringLiteral(".ss"));
    return format;
}

}  // namespace

Format::Format(QObject* parent)
    : QObject(parent)
{
}

QDateTime Format::toDateTime(const QVariant& value)
{
    if (!value.isValid() || value.isNull())
        return {};
    if (value.metaType().id() == QMetaType::QDateTime)
        return value.toDateTime();
    bool ok = false;
    const qint64 ms = value.toLongLong(&ok);
    if (ok && ms > 0)
        return QDateTime::fromMSecsSinceEpoch(ms);
    return value.toDateTime();
}

QString Format::duration(qint64 seconds) const
{
    if (seconds < 0)
        return kUnknown;
    if (seconds < 60)
        return tr("%1 s").arg(seconds);
    const qint64 minutes = (seconds + 30) / 60;
    if (minutes < 60)
        return tr("%1 min").arg(minutes);
    const qint64 hours = minutes / 60;
    const qint64 rest = minutes % 60;
    if (hours >= 24) {
        const qint64 days = hours / 24;
        return tr("%1 d %2 h").arg(days).arg(hours % 24);
    }
    return rest == 0 ? tr("%1 h").arg(hours) : tr("%1 h %2 min").arg(hours).arg(rest);
}

QString Format::durationShort(qint64 seconds) const
{
    if (seconds < 0)
        return kUnknown;
    if (seconds < 60)
        return tr("%1 s").arg(seconds);
    const qint64 minutes = (seconds + 30) / 60;
    if (minutes < 60)
        return tr("%1 min").arg(minutes);
    return tr("%1:%2 h").arg(minutes / 60).arg(minutes % 60, 2, 10, QLatin1Char('0'));
}

QString Format::percent(qreal value) const
{
    if (value < 0 || std::isnan(value))
        return kUnknown;
    return tr("%1 %").arg(QLocale().toString(std::round(value), 'f', 0));
}

QString Format::watts(qreal value) const
{
    if (value < 0 || std::isnan(value))
        return kUnknown;
    if (value >= 1000)
        return tr("%1 kW").arg(QLocale().toString(value / 1000.0, 'f', 2));
    return tr("%1 W").arg(QLocale().toString(std::round(value), 'f', 0));
}

QString Format::volts(qreal value) const
{
    if (value < 0 || std::isnan(value))
        return kUnknown;
    return tr("%1 V").arg(QLocale().toString(value, 'f', value < 100 ? 1 : 0));
}

QString Format::energy(qreal wattHours) const
{
    if (wattHours < 0 || std::isnan(wattHours))
        return kUnknown;
    if (wattHours < 1000)
        return tr("%1 Wh").arg(QLocale().toString(std::round(wattHours), 'f', 0));
    const qreal kwh = wattHours / 1000.0;
    if (kwh < 100)
        return tr("%1 kWh").arg(QLocale().toString(kwh, 'f', 1));
    if (kwh < 10000)
        return tr("%1 kWh").arg(QLocale().toString(std::round(kwh), 'f', 0));
    return tr("%1 MWh").arg(QLocale().toString(kwh / 1000.0, 'f', 2));
}

QString Format::celsius(qreal value) const
{
    if (std::isnan(value))
        return kUnknown;
    return tr("%1 °C").arg(
        QLocale().toString(value, 'f', std::abs(value - std::round(value)) < 0.05 ? 0 : 1));
}

QString Format::relativeTime(const QVariant& when, const QVariant& now) const
{
    const QDateTime t = toDateTime(when);
    if (!t.isValid())
        return kUnknown;
    QDateTime reference = toDateTime(now);
    if (!reference.isValid())
        reference = QDateTime::currentDateTime();
    const qint64 secs = t.secsTo(reference);
    if (secs < 5)
        return tr("just now");
    if (secs < 60)
        return tr("%1 s ago").arg(secs);
    if (secs < 3600)
        return tr("%1 min ago").arg(secs / 60);
    if (secs < 86400)
        return tr("%1 h ago").arg(secs / 3600);
    return smartDateTime(when);
}

QString Format::dateTime(const QVariant& when) const
{
    const QDateTime t = toDateTime(when);
    if (!t.isValid())
        return kUnknown;
    const QDateTime local = t.toLocalTime();
    return QLocale().toString(local.date(), QLocale::ShortFormat) + u' '
        + QLocale().toString(local.time(), shortTimeFormat());
}

QString Format::time(const QVariant& when) const
{
    const QDateTime t = toDateTime(when);
    if (!t.isValid())
        return kUnknown;
    return QLocale().toString(t.toLocalTime().time(), shortTimeFormat());
}

QString Format::smartDateTime(const QVariant& when) const
{
    const QDateTime t = toDateTime(when);
    if (!t.isValid())
        return kUnknown;
    const QDateTime local = t.toLocalTime();
    const QDate today = QDate::currentDate();
    if (local.date() == today)
        return time(when);
    if (local.date() == today.addDays(-1))
        return tr("Yesterday %1").arg(time(when));
    return QLocale().toString(local.date(), QLocale::ShortFormat) + u' ' + time(when);
}

}  // namespace nutty
