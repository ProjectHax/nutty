// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

namespace nutty {

// Locale-aware, translatable formatting for QML, as the singleton `Format`.
// Negative numbers mean "unknown" everywhere and format as an em dash.
class Format : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit Format(QObject* parent = nullptr);

    // "1 h 24 min", "18 min", "45 s".
    Q_INVOKABLE QString duration(qint64 seconds) const;
    // "1:24 h", "18 min" — compact, for small labels.
    Q_INVOKABLE QString durationShort(qint64 seconds) const;
    // "63 %".
    Q_INVOKABLE QString percent(qreal value) const;
    // "210 W", "1.25 kW".
    Q_INVOKABLE QString watts(qreal value) const;
    // "230 V".
    Q_INVOKABLE QString volts(qreal value) const;
    // "545 Wh", "12.4 kWh", "8,596 kWh", "1.22 MWh".
    Q_INVOKABLE QString energy(qreal wattHours) const;
    // "26 °C".
    Q_INVOKABLE QString celsius(qreal value) const;
    // "just now", "12 s ago", "5 min ago", "3 h ago", then a date. `when` and
    // `now` accept a JS Date or milliseconds since the epoch; `now` defaults
    // to the current time.
    Q_INVOKABLE QString relativeTime(const QVariant& when, const QVariant& now = {}) const;
    // Local date and time in the locale's short format.
    Q_INVOKABLE QString dateTime(const QVariant& when) const;
    // Local time of day, e.g. "14:02".
    Q_INVOKABLE QString time(const QVariant& when) const;
    // Today → time; otherwise a short date and time.
    Q_INVOKABLE QString smartDateTime(const QVariant& when) const;

    static QDateTime toDateTime(const QVariant& value);
};

}  // namespace nutty
