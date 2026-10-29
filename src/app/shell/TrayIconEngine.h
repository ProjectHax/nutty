// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Types.h"

#include <QColor>
#include <QIconEngine>
#include <QString>

namespace nutty {

// What the tray icon shows: the worst device's charge and state.
struct TrayIconState {
    TrayIconStyle style = TrayIconStyle::Acorn;
    int charge = -1;  // percent; < 0 unknown
    // ok, unknown, warning, offline, onBattery, low, critical (deviceinfo::healthKey)
    QString health = QStringLiteral("unknown");
    bool charging = false;
    bool pending = false;  // a power action is counting down
    bool monochrome = false;  // one colour, for monochrome panels
    bool mask = false;  // macOS template image: black with alpha only
    bool darkPanel = true;  // the colour monochrome icons are drawn for

    bool operator==(const TrayIconState&) const = default;
};

// Paints the tray icon, an acorn or a battery that fills up with the charge,
// at any size. It fills its whole square and keeps straight edges on whole
// pixels, so it stays crisp and as big as the panel's own icons. The tray gets
// a new QIcon with a new engine whenever the state changes, because tray
// backends cache what an icon renders to.
class TrayIconEngine : public QIconEngine
{
public:
    explicit TrayIconEngine(const TrayIconState& state);

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override;
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override;
    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override;
    QList<QSize> availableSizes(QIcon::Mode mode = QIcon::Normal, QIcon::State state = QIcon::Off) override;
    QIconEngine* clone() const override;
    QString key() const override;

    // The fill colour for a state (when not monochrome).
    static QColor levelColor(const TrayIconState& state);
    // Renders the icon into an image, for tests and the tray fallback.
    static QImage render(const TrayIconState& state, int size, qreal dpr = 1.0);

private:
    TrayIconState m_state;
};

}  // namespace nutty
