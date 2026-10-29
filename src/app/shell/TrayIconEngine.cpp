// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "app/shell/TrayIconEngine.h"

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <algorithm>
#include <cmath>

namespace nutty {

namespace {

int px(qreal v)
{
    return static_cast<int>(std::lround(v));
}

// Every outline runs the same way round, so under the winding rule shapes that
// overlap stay filled and a reversed outline cuts a hole. (Path set operations
// would do the same but flatten curves too coarsely for a 16 px icon.)
QPainterPath clockwise(const QPainterPath& path)
{
    const QPolygonF poly = path.toFillPolygon();
    qreal area = 0;
    for (qsizetype i = 0; i < poly.size(); ++i) {
        const QPointF a = poly[i];
        const QPointF b = poly[(i + 1) % poly.size()];
        area += a.x() * b.y() - b.x() * a.y();
    }
    return area >= 0 ? path : path.toReversed();
}

QPainterPath hole(const QPainterPath& path)
{
    return clockwise(path).toReversed();
}

// The shape of one style at one size, in pixels from the top left.
struct Glyph {
    QPainterPath ink;  // drawn in the panel's colour
    QPainterPath well;  // where the charge fills
    bool rises = false;  // the charge fills the well from the bottom, not the left
    QPointF boltCentre;
    qreal boltHeight = 0;
};

// A horizontal battery with its terminal on the right, as wide as the square.
Glyph battery(int size)
{
    Glyph g;
    const int stroke = std::max(1, px(size * 0.11));
    const int nub = std::max(2, px(size * 0.1));
    const int margin = px(size * 0.17);
    const QRectF body(0, margin, size - nub, size - 2 * margin);
    const qreal radius = std::max<qreal>(1.5, size * 0.14);
    const QRectF inner = body.adjusted(stroke, stroke, -stroke, -stroke);
    const qreal innerRadius = std::max<qreal>(0.5, radius - stroke);
    QPainterPath outline;
    outline.addRoundedRect(body, radius, radius);
    QPainterPath inside;
    inside.addRoundedRect(inner, innerRadius, innerRadius);
    // The terminal reaches back into the wall (not past it), so they join without a seam.
    const int nubTop = px(size * 0.34);
    const int overlap = std::min(nub, stroke);
    QPainterPath terminal;
    terminal.addRoundedRect(
        QRectF(size - nub - overlap, nubTop, nub + overlap, size - 2 * nubTop), nub * 0.5, nub * 0.5);
    g.ink.setFillRule(Qt::WindingFill);
    g.ink.addPath(clockwise(outline));
    g.ink.addPath(hole(inside));
    g.ink.addPath(clockwise(terminal));

    const int gap = std::max(1, px(size * 0.06));
    const qreal wellRadius = std::max<qreal>(0.0, innerRadius - gap);
    g.well.addRoundedRect(inner.adjusted(gap, gap, -gap, -gap), wellRadius, wellRadius);
    g.boltCentre = body.center();
    g.boltHeight = body.height() * 0.95;
    return g;
}

// The acorn body below the cap, a rounded V, inset by `inset` from its outline.
QPainterPath acornBody(int size, qreal top, qreal side, qreal inset)
{
    const qreal left = side + inset;
    const qreal right = size - side - inset;
    const qreal tip = size - inset * 1.8;
    const qreal h = tip - top;
    const qreal mid = size / 2.0;
    const qreal pull = (mid - left) * 0.55;
    QPainterPath p;
    p.moveTo(left, top);
    p.lineTo(right, top);
    p.cubicTo(right, top + h * 0.6, mid + pull, tip - h * 0.14, mid, tip);
    p.cubicTo(mid - pull, tip - h * 0.14, left, top + h * 0.6, left, top);
    p.closeSubpath();
    return p;
}

// nutty's acorn: a stem, a solid cap and, under it, a shell that holds the
// charge. The shell is open at the top, so a full charge reaches the cap.
// `gapped` leaves a gap between the shell and the charge, for one colour.
Glyph acorn(int size, bool gapped)
{
    Glyph g;
    const int stroke = std::max(1, px(size * 0.11));
    const int stemWidth = 2 * std::max(1, px(size * 0.07));
    const int capTop = px(size * 0.12);
    const int capBottom = px(size * 0.4);
    const qreal capHeight = capBottom - capTop;
    const qreal lip = std::max<qreal>(1.0, size * 0.06);

    QPainterPath stem;
    stem.addRoundedRect(QRectF((size - stemWidth) / 2.0, 0, stemWidth, capTop + capHeight / 2),
        stemWidth * 0.35, stemWidth * 0.35);
    QPainterPath cap;
    cap.moveTo(0, capBottom - lip);
    cap.cubicTo(0, capTop + capHeight * 0.1, size * 0.2, capTop, size * 0.5, capTop);
    cap.cubicTo(size * 0.8, capTop, size, capTop + capHeight * 0.1, size, capBottom - lip);
    cap.quadTo(size, capBottom, size - lip, capBottom);
    cap.lineTo(lip, capBottom);
    cap.quadTo(0, capBottom, 0, capBottom - lip);
    cap.closeSubpath();

    const int top = capBottom + std::max(1, px(size * 0.06));
    const int side = px(size * 0.14);
    g.ink.setFillRule(Qt::WindingFill);
    g.ink.addPath(clockwise(stem));
    g.ink.addPath(clockwise(cap));
    g.ink.addPath(clockwise(acornBody(size, top, side, 0)));
    g.ink.addPath(hole(acornBody(size, top, side, stroke)));

    const int gap = gapped ? std::max(1, px(size * 0.06)) : 0;
    g.well = acornBody(size, top, side, stroke + gap);
    g.rises = true;
    // The bolt runs through the cap too; the shell alone is too small for one.
    g.boltCentre = QPointF(size / 2.0, size * 0.56);
    g.boltHeight = size * 0.8;
    return g;
}

}  // namespace

TrayIconEngine::TrayIconEngine(const TrayIconState& state)
    : m_state(state)
{
}

QColor TrayIconEngine::levelColor(const TrayIconState& s)
{
    if (s.health == u"low" || s.health == u"critical" || s.pending)
        return QColor(0xff, 0x45, 0x3a);
    if (s.health == u"onBattery" || s.health == u"warning")
        return QColor(0xff, 0x9f, 0x0a);
    if (s.health == u"offline" || s.health == u"unknown")
        return QColor(0x8e, 0x8e, 0x93);
    return QColor(0x30, 0xd1, 0x58);
}

void TrayIconEngine::paint(QPainter* p, const QRect& rect, QIcon::Mode mode, QIcon::State)
{
    const TrayIconState& s = m_state;
    // Whole pixels, so straight edges land on the pixel grid.
    const int size = std::min(rect.width(), rect.height());
    const QPoint origin(rect.x() + (rect.width() - size) / 2, rect.y() + (rect.height() - size) / 2);

    const QColor ink = s.mask ? QColor(Qt::black)
        : s.darkPanel         ? QColor(0xf2, 0xf2, 0xf2)
                              : QColor(0x22, 0x22, 0x26);
    const bool oneColour = s.mask || s.monochrome;
    const QColor fill = oneColour ? ink : levelColor(s);
    const bool offline = s.health == u"offline";
    // In one colour the acorn's charge needs a gap to stand apart from its shell.
    const Glyph g = s.style == TrayIconStyle::Battery ? battery(size) : acorn(size, oneColour);

    p->save();
    p->translate(origin);
    p->setRenderHint(QPainter::Antialiasing);
    if (mode == QIcon::Disabled)
        p->setOpacity(0.5);
    const qreal opacity = p->opacity();

    p->setOpacity(opacity * (offline ? 0.55 : 1.0));
    p->fillPath(g.ink, ink);

    // The charge: never invisible, a sliver stays for an empty battery.
    if (s.charge >= 0 && !offline) {
        const qreal level = std::clamp(s.charge, 0, 100) / 100.0;
        const QRectF well = g.well.boundingRect();
        // Clipped to whole pixels, so the level's edge is crisp.
        QRect part = well.toAlignedRect();
        if (g.rises) {
            const qreal h = std::max(well.height() * level, std::max<qreal>(1.5, well.height() * 0.12));
            part.setTop(px(well.bottom() - h));
        } else {
            const qreal w = std::max(well.width() * level, std::max<qreal>(1.0, well.width() * 0.08));
            part.setRight(px(well.left() + w) - 1);
        }
        p->save();
        p->setClipRect(part);
        p->fillPath(g.well, fill);
        p->restore();
    }

    // A bolt while charging, cut out of the fill with a ring of the panel colour.
    if (s.charging && !offline) {
        QPainterPath bolt;
        const QPointF c = g.boltCentre;
        const qreal h = g.boltHeight;
        const qreal w = h * 0.62;
        bolt.moveTo(c.x() + w * 0.12, c.y() - h / 2);
        bolt.lineTo(c.x() - w * 0.42, c.y() + h * 0.06);
        bolt.lineTo(c.x() - w * 0.02, c.y() + h * 0.06);
        bolt.lineTo(c.x() - w * 0.14, c.y() + h / 2);
        bolt.lineTo(c.x() + w * 0.42, c.y() - h * 0.08);
        bolt.lineTo(c.x() + w * 0.02, c.y() - h * 0.08);
        bolt.closeSubpath();
        if (!s.mask) {
            p->setCompositionMode(QPainter::CompositionMode_Clear);
            p->strokePath(bolt,
                QPen(Qt::black, std::max<qreal>(1.0, size * 0.07), Qt::SolidLine, Qt::RoundCap,
                    Qt::RoundJoin));
            p->setCompositionMode(QPainter::CompositionMode_SourceOver);
        }
        p->fillPath(bolt, s.mask ? QColor(Qt::black) : ink);
    }

    // Lost contact: a slash across.
    if (offline) {
        p->setOpacity(opacity);
        p->setPen(QPen(ink, std::max<qreal>(1.0, std::round(size * 0.11)), Qt::SolidLine, Qt::RoundCap));
        p->drawLine(QPointF(size * 0.12, size * 0.88), QPointF(size * 0.88, size * 0.12));
    }

    // A pending power action: a dot in the corner.
    if (s.pending) {
        p->setOpacity(opacity);
        const qreal r = size * 0.17;
        const QPointF centre(size - r - size * 0.01, r + size * 0.04);
        if (!s.mask) {
            p->setCompositionMode(QPainter::CompositionMode_Clear);
            p->setPen(Qt::NoPen);
            p->setBrush(Qt::black);
            p->drawEllipse(
                centre, r + std::max<qreal>(1.0, size * 0.05), r + std::max<qreal>(1.0, size * 0.05));
            p->setCompositionMode(QPainter::CompositionMode_SourceOver);
        }
        p->setPen(Qt::NoPen);
        p->setBrush(oneColour ? ink : levelColor(s));
        p->drawEllipse(centre, r, r);
    }
    p->restore();
}

QImage TrayIconEngine::render(const TrayIconState& state, int size, qreal dpr)
{
    const int pixels = std::max(1, px(size * dpr));
    QImage image(pixels, pixels, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        TrayIconEngine(state).paint(&painter, QRect(0, 0, pixels, pixels), QIcon::Normal, QIcon::Off);
    }
    image.setDevicePixelRatio(dpr);
    return image;
}

QPixmap TrayIconEngine::pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state)
{
    return scaledPixmap(size, mode, state, 1.0);
}

QPixmap TrayIconEngine::scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale)
{
    const QSize pixels = (QSizeF(size) * scale).toSize().expandedTo(QSize(1, 1));
    QImage image(pixels, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        paint(&painter, QRect(QPoint(0, 0), pixels), mode, state);
    }
    QPixmap pixmap = QPixmap::fromImage(std::move(image));
    pixmap.setDevicePixelRatio(scale);
    return pixmap;
}

QList<QSize> TrayIconEngine::availableSizes(QIcon::Mode, QIcon::State)
{
    // Panels that are sent pixmaps (StatusNotifierItem) pick the smallest one
    // at least as big as their icon size, then scale it down; a close size
    // stays sharp. These cover 16-24 px panels at 1x and 2x.
    QList<QSize> sizes;
    for (int s : { 16, 18, 20, 22, 24, 28, 32, 36, 40, 44, 48, 64 })
        sizes << QSize(s, s);
    return sizes;
}

QIconEngine* TrayIconEngine::clone() const
{
    return new TrayIconEngine(m_state);
}

QString TrayIconEngine::key() const
{
    return QStringLiteral("nutty-tray");
}

}  // namespace nutty
