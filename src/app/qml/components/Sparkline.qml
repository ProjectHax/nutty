// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Shapes
import Nutty

// A small line chart with a gradient area under it. `values` are evenly
// spaced samples, oldest first; negative entries are skipped.
Item {
    id: root

    // Time runs left to right in every language.
    LayoutMirroring.enabled: false
    LayoutMirroring.childrenInherit: true

    property var values: []
    property real minValue: 0
    property real maxValue: 100
    property color color: Theme.accent
    property real lineWidth: 2
    property bool fill: true
    property bool showLastPoint: true

    implicitWidth: 160
    implicitHeight: 40

    readonly property var series: build(values, width, height)

    function build(vals, w, h) {
        const out = { line: [], area: [], last: null }
        if (!vals || vals.length < 2 || w <= 0 || h <= 0)
            return out
        const n = vals.length
        const step = Math.max(1, Math.floor(n / Math.max(2, w / 2)))   // ~1 point per 2 px
        const span = Math.max(0.0001, maxValue - minValue)
        const pad = lineWidth
        for (let i = 0; i < n; i += step) {
            const v = vals[i]
            if (v === undefined || v === null || v < 0)
                continue
            const x = i / (n - 1) * w
            const y = pad + (1 - (Math.min(maxValue, Math.max(minValue, v)) - minValue) / span) * (h - 2 * pad)
            out.line.push(Qt.point(x, y))
        }
        const lastV = vals[n - 1]
        if (lastV !== undefined && lastV >= 0) {
            const y = pad + (1 - (Math.min(maxValue, Math.max(minValue, lastV)) - minValue) / span) * (h - 2 * pad)
            if (out.line.length === 0 || out.line[out.line.length - 1].x < w)
                out.line.push(Qt.point(w, y))
            out.last = Qt.point(w, y)
        }
        if (out.line.length > 1) {
            out.area = out.line.slice()
            out.area.push(Qt.point(out.line[out.line.length - 1].x, h))
            out.area.push(Qt.point(out.line[0].x, h))
        }
        return out
    }

    Shape {
        anchors.fill: parent
        visible: root.series.line.length > 1
        layer.enabled: Theme.effectsEnabled
        layer.samples: 4

        ShapePath {
            strokeColor: "transparent"
            strokeWidth: 0
            fillGradient: LinearGradient {
                x1: 0; y1: 0
                x2: 0; y2: root.height
                GradientStop { position: 0; color: Theme.alpha(root.color, root.fill ? (Theme.dark ? 0.16 : 0.12) : 0) }
                GradientStop { position: 1; color: Theme.alpha(root.color, 0) }
            }
            PathPolyline { path: root.series.area }
        }
        ShapePath {
            strokeColor: root.color
            strokeWidth: root.lineWidth
            fillColor: "transparent"
            joinStyle: ShapePath.RoundJoin
            capStyle: ShapePath.RoundCap
            PathPolyline { path: root.series.line }
        }
    }

    Rectangle {
        visible: root.showLastPoint && root.series.last !== null
        width: root.lineWidth * 3.5
        height: width
        radius: width / 2
        color: root.color
        border.width: 2
        border.color: Theme.card
        x: root.series.last ? root.series.last.x - width / 2 - 1 : 0
        y: root.series.last ? root.series.last.y - height / 2 : 0
    }
}
