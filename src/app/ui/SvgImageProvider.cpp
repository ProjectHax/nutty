// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "app/ui/SvgImageProvider.h"

#include <QColor>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QRegularExpression>
#include <QSvgRenderer>

namespace nutty {

SvgImageProvider::SvgImageProvider(Kind kind)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_kind(kind)
{
}

QByteArray SvgImageProvider::svgData(const QString& name)
{
    static const QRegularExpression valid(QStringLiteral("^[a-z0-9-]+$"));
    if (!valid.match(name).hasMatch())
        return {};

    QMutexLocker lock(&m_mutex);
    const auto it = m_cache.constFind(name);
    if (it != m_cache.cend())
        return *it;
    const QString dir
        = m_kind == Kind::Icons ? QStringLiteral(":/nutty/icons/") : QStringLiteral(":/nutty/art/");
    QFile file(dir + name + QStringLiteral(".svg"));
    QByteArray data;
    if (file.open(QIODevice::ReadOnly))
        data = file.readAll();
    m_cache.insert(name, data);
    return data;
}

QImage SvgImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    QString name = id;
    QColor color(Qt::black);
    if (m_kind == Kind::Icons) {
        const qsizetype slash = id.indexOf(u'/');
        if (slash >= 0) {
            name = id.left(slash);
            const QString hex = id.mid(slash + 1);
            const QColor parsed(u'#' + hex);
            if (parsed.isValid())
                color = parsed;
        }
    }

    QByteArray data = svgData(name);
    if (data.isEmpty()) {
        if (size)
            *size = QSize(1, 1);
        QImage empty(1, 1, QImage::Format_ARGB32_Premultiplied);
        empty.fill(Qt::transparent);
        return empty;
    }
    if (m_kind == Kind::Icons)
        data.replace("currentColor", color.name(QColor::HexRgb).toLatin1());

    QSvgRenderer renderer(data);
    const QSize natural = renderer.defaultSize().isValid() ? renderer.defaultSize() : QSize(24, 24);
    QSize target = natural;
    if (requestedSize.width() > 0 && requestedSize.height() > 0)
        target = natural.scaled(requestedSize, Qt::KeepAspectRatio);
    else if (requestedSize.width() > 0)
        target = natural.scaled(requestedSize.width(), 1 << 20, Qt::KeepAspectRatio);
    else if (requestedSize.height() > 0)
        target = natural.scaled(1 << 20, requestedSize.height(), Qt::KeepAspectRatio);
    target = target.expandedTo(QSize(1, 1));

    QImage image(target, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        if (m_kind == Kind::Icons && color.alphaF() < 1.0)
            painter.setOpacity(color.alphaF());
        renderer.render(&painter, QRectF(QPointF(0, 0), QSizeF(target)));
    }
    if (size)
        *size = target;
    return image;
}

}  // namespace nutty
