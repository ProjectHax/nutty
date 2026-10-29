// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Shapes

// A rounded-rectangle ShapePath (PathRectangle needs Qt 6.8). The rectangle
// is inset by half the stroke so the stroke stays inside w × h.
ShapePath {
    id: path

    property real w: 0
    property real h: 0
    property real r: 0
    readonly property real i: strokeWidth / 2
    readonly property real rr: Math.max(0, Math.min(r, (w - 2 * i) / 2, (h - 2 * i) / 2))

    startX: i + rr
    startY: i
    PathLine { x: path.w - path.i - path.rr; y: path.i }
    PathArc { x: path.w - path.i; y: path.i + path.rr; radiusX: path.rr; radiusY: path.rr }
    PathLine { x: path.w - path.i; y: path.h - path.i - path.rr }
    PathArc { x: path.w - path.i - path.rr; y: path.h - path.i; radiusX: path.rr; radiusY: path.rr }
    PathLine { x: path.i + path.rr; y: path.h - path.i }
    PathArc { x: path.i; y: path.h - path.i - path.rr; radiusX: path.rr; radiusY: path.rr }
    PathLine { x: path.i; y: path.i + path.rr }
    PathArc { x: path.i + path.rr; y: path.i; radiusX: path.rr; radiusY: path.rr }
}
