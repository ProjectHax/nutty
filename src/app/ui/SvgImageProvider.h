// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include <QByteArray>
#include <QHash>
#include <QMutex>
#include <QQuickImageProvider>
#include <QString>

namespace nutty {

// Renders the bundled SVGs through QSvgRenderer, so they are crisp at any
// size and DPR and need no image-format plugin at runtime.
//
//   image://icons/<name>/<rrggbb|aarrggbb>   resources/icons/<name>.svg, with
//                                            every "currentColor" replaced
//   image://art/<name>                       resources/art/<name>.svg, as is
//
// Pass sourceSize in device pixels (logical size × devicePixelRatio).
class SvgImageProvider : public QQuickImageProvider
{
public:
    enum class Kind { Icons, Art };

    explicit SvgImageProvider(Kind kind);

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
    QByteArray svgData(const QString& name);

    Kind m_kind;
    QMutex m_mutex;
    QHash<QString, QByteArray> m_cache;
};

}  // namespace nutty
