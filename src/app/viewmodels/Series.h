// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

namespace nutty {

class AppContext;

// Chart data from the history database, as the singleton `Series`.
//
//   RangeChart {
//       readonly property var data: Series.revision, Series.get(id, "charge", from, to, 240)
//       points: data.points; bands: data.bands
//   }
//
// `revision` changes when new samples are worth redrawing, so bindings that
// mention it refresh on their own.
class Series : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    explicit Series(AppContext* context = nullptr, QObject* parent = nullptr);
    static Series* create(QQmlEngine*, QJSEngine*);

    int revision() const { return m_revision; }

    // metric: charge, runtime (minutes), load, wattsIn, wattsOut, wattsSolar,
    // inputVoltage, temperature.
    // from/to: ms since the epoch. Returns { points: [{x, y}], bands: [{from, to}],
    // min, max, last } with x in ms; bands mark time on battery.
    Q_INVOKABLE QVariantMap get(
        const QString& deviceId, const QString& metric, double from, double to, int maxPoints = 240) const;
    // Energy per day for the last `days` days, today included:
    // { days: [{ x: <ms at local midnight>, whIn, whOut, whSolar }],
    //   whIn, whOut, whSolar (the totals), max (the biggest day's in or out) }.
    Q_INVOKABLE QVariantMap energy(const QString& deviceId, int days) const;
    // The current time as the monitor sees it (ms since the epoch).
    Q_INVOKABLE double now() const;

signals:
    void revisionChanged();

private:
    void bump();

    QPointer<AppContext> m_context;
    int m_revision = 0;
    QTimer m_throttle;
    bool m_dirty = false;
};

}  // namespace nutty
