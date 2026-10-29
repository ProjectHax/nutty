// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Shapes
import Nutty

// A time-series chart with a grid, axis labels, shaded bands (e.g. time on
// battery) and a hover readout.
//
//   points: [{ x: <ms since epoch>, y: <value> }, ...] sorted by x
//   points2: an optional second line (no fill), e.g. power in beside power out
//   bands:  [{ from: <ms>, to: <ms> }, ...]
Item {
    id: root

    // Time runs left to right in every language.
    LayoutMirroring.enabled: false
    LayoutMirroring.childrenInherit: true

    property var points: []
    property var bands: []
    property real xFrom: points.length > 0 ? points[0].x : 0
    property real xTo: points.length > 0 ? points[points.length - 1].x : 1
    property real yMin: 0
    property real yMax: 100
    property string yUnit: "%"
    property int yTicks: 4
    property int xTicks: 6
    property color color: Theme.accent
    property var points2: []
    property color color2: Theme.success
    // Names for the two lines in the hover readout (when there are two).
    property string label
    property string label2
    property color bandColor: Theme.warning
    property string bandLabel: qsTr("On battery")
    property var formatY: function(v) { return Math.round(v) + (yUnit.length > 0 ? " " + yUnit : "") }

    readonly property real leftPad: 44
    readonly property real rightPad: 10
    readonly property real topPad: 10
    readonly property real bottomPad: 24
    readonly property real spanMs: Math.max(1, xTo - xFrom)

    implicitWidth: 480
    implicitHeight: 220

    // A little headroom so a line at yMax isn't cut in half.
    readonly property real headroom: 3

    function px(x) { return (x - xFrom) / spanMs * plot.width }
    function py(y) {
        const t = (Math.max(yMin, Math.min(yMax, y)) - yMin) / Math.max(0.0001, yMax - yMin)
        return headroom + (plot.height - headroom) * (1 - t)
    }

    readonly property bool shortSpan: spanMs <= 36 * 3600 * 1000
    readonly property string timeFormat: Qt.locale().timeFormat(Locale.ShortFormat).replace(/[:.]ss/, "")

    function xLabel(ms) {
        const d = new Date(ms)
        return shortSpan ? d.toLocaleTimeString(Qt.locale(), timeFormat) : d.toLocaleDateString(Qt.locale(), "d MMM")
    }

    // Tick positions on round hours (or days), at most xTicks + 1 of them.
    readonly property var xTickValues: {
        const hour = 3600 * 1000
        const steps = shortSpan ? [1, 2, 3, 4, 6, 8, 12, 24].map(h => h * hour)
                                : [1, 2, 3, 7, 14, 30].map(d => d * 24 * hour)
        let step = steps[steps.length - 1]
        for (const s of steps) {
            if (spanMs / s <= xTicks) {
                step = s
                break
            }
        }
        // Align to local midnight so labels land on 00:00, 06:00, ...
        const first = new Date(xFrom)
        first.setHours(0, 0, 0, 0)
        let t = first.getTime()
        while (t < xFrom)
            t += step
        const ticks = []
        for (; t <= xTo && ticks.length <= xTicks + 1; t += step)
            ticks.push(t)
        return ticks
    }

    function nearest(x, list) {
        const pts = list || points
        if (!pts || pts.length === 0)
            return -1
        const target = xFrom + x / Math.max(1, plot.width) * spanMs
        let lo = 0, hi = pts.length - 1
        while (hi - lo > 1) {
            const mid = (lo + hi) >> 1
            if (pts[mid].x < target) lo = mid
            else hi = mid
        }
        return Math.abs(pts[lo].x - target) <= Math.abs(pts[hi].x - target) ? lo : hi
    }

    function polyline(pts) {
        const line = []
        const w = plot.width, h = plot.height
        if (pts && pts.length > 1 && w > 0 && h > 0) {
            const step = Math.max(1, Math.floor(pts.length / Math.max(2, w / 1.5)))
            for (let i = 0; i < pts.length; i += step)
                line.push(Qt.point(px(pts[i].x), py(pts[i].y)))
            const last = pts[pts.length - 1]
            line.push(Qt.point(px(last.x), py(last.y)))
        }
        return line
    }
    readonly property var series2: polyline(points2)

    readonly property var series: {
        const line = []
        const w = plot.width, h = plot.height
        const pts = points
        if (pts && pts.length > 1 && w > 0 && h > 0) {
            const step = Math.max(1, Math.floor(pts.length / Math.max(2, w / 1.5)))
            for (let i = 0; i < pts.length; i += step)
                line.push(Qt.point(px(pts[i].x), py(pts[i].y)))
            const last = pts[pts.length - 1]
            line.push(Qt.point(px(last.x), py(last.y)))
        }
        const area = line.slice()
        if (line.length > 1) {
            area.push(Qt.point(line[line.length - 1].x, h))
            area.push(Qt.point(line[0].x, h))
        }
        return { line: line, area: area }
    }

    // Y grid + labels.
    Repeater {
        model: root.yTicks + 1
        Item {
            required property int index
            readonly property real value: root.yMin + (root.yMax - root.yMin) * index / root.yTicks
            x: 0
            y: root.topPad + root.py(value)
            width: root.width
            Rectangle {
                x: root.leftPad
                width: root.width - root.leftPad - root.rightPad
                height: 1
                color: Theme.divider
            }
            Text {
                anchors.right: parent.left
                anchors.rightMargin: -root.leftPad + 8
                anchors.verticalCenter: parent.top
                text: root.formatY(parent.value)
                color: Theme.faint
                font.pixelSize: Theme.fontSizeSmall
            }
        }
    }

    // X labels.
    Repeater {
        model: root.xTickValues
        Item {
            required property real modelData
            x: root.leftPad + root.px(modelData)
            y: root.topPad
            Text {
                x: Math.max(-parent.x, Math.min(root.width - parent.x - implicitWidth, -implicitWidth / 2))
                y: plot.height + 6
                text: root.xLabel(parent.modelData)
                color: Theme.faint
                font.pixelSize: Theme.fontSizeSmall
            }
        }
    }

    Item {
        id: plot
        x: root.leftPad
        y: root.topPad
        width: root.width - root.leftPad - root.rightPad
        height: root.height - root.topPad - root.bottomPad
        clip: true

        Repeater {
            model: root.bands
            Rectangle {
                required property var modelData
                x: Math.max(0, root.px(modelData.from))
                width: Math.max(2, Math.min(plot.width, root.px(modelData.to)) - x)
                y: 0
                height: plot.height
                color: Theme.alpha(root.bandColor, Theme.dark ? 0.14 : 0.10)
                Rectangle { width: parent.width; height: 2; color: Theme.alpha(root.bandColor, 0.55) }
            }
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
                    x2: 0; y2: plot.height
                    GradientStop { position: 0; color: Theme.alpha(root.color, Theme.dark ? 0.16 : 0.12) }
                    GradientStop { position: 1; color: Theme.alpha(root.color, 0) }
                }
                PathPolyline { path: root.series.area }
            }
            ShapePath {
                strokeColor: root.color
                strokeWidth: 2
                fillColor: "transparent"
                joinStyle: ShapePath.RoundJoin
                capStyle: ShapePath.RoundCap
                PathPolyline { path: root.series.line }
            }
        }

        Shape {
            anchors.fill: parent
            visible: root.series2.length > 1
            layer.enabled: Theme.effectsEnabled
            layer.samples: 4
            ShapePath {
                strokeColor: root.color2
                strokeWidth: 2
                fillColor: "transparent"
                joinStyle: ShapePath.RoundJoin
                capStyle: ShapePath.RoundCap
                PathPolyline { path: root.series2 }
            }
        }

        // Hover readout.
        HoverHandler { id: hover }
        Item {
            id: readout
            readonly property int index: hover.hovered ? root.nearest(hover.point.position.x) : -1
            readonly property var pt: index >= 0 ? root.points[index] : null
            readonly property int index2: hover.hovered && root.points2.length > 0
                                          ? root.nearest(hover.point.position.x, root.points2) : -1
            readonly property var pt2: index2 >= 0 ? root.points2[index2] : null
            visible: pt !== null || pt2 !== null
            anchors.fill: parent

            readonly property var at: pt || pt2
            Rectangle {
                x: readout.at ? root.px(readout.at.x) : 0
                width: 1
                height: plot.height
                color: Theme.alpha(Theme.text, 0.25)
            }
            Rectangle {
                visible: readout.pt !== null
                width: 10
                height: 10
                radius: 5
                color: root.color
                border.width: 2
                border.color: Theme.card
                x: readout.pt ? root.px(readout.pt.x) - 5 : 0
                y: readout.pt ? root.py(readout.pt.y) - 5 : 0
            }
            Rectangle {
                visible: readout.pt2 !== null
                width: 10
                height: 10
                radius: 5
                color: root.color2
                border.width: 2
                border.color: Theme.card
                x: readout.pt2 ? root.px(readout.pt2.x) - 5 : 0
                y: readout.pt2 ? root.py(readout.pt2.y) - 5 : 0
            }
            Rectangle {
                id: tip
                readonly property real ax: readout.at ? root.px(readout.at.x) : 0
                x: ax + 12 + width > plot.width ? ax - 12 - width : ax + 12
                y: 6
                width: tipColumn.implicitWidth + 16
                height: tipColumn.implicitHeight + 10
                radius: Theme.radiusSmall
                color: Theme.elevated
                border.width: 1
                border.color: Theme.border
                Column {
                    id: tipColumn
                    anchors.centerIn: parent
                    Text {
                        visible: readout.pt !== null
                        text: readout.pt ? (root.label2.length > 0 && root.label.length > 0
                                            ? root.label + " " : "") + root.formatY(readout.pt.y) : ""
                        color: Theme.text
                        font.weight: Font.DemiBold
                    }
                    Text {
                        visible: readout.pt2 !== null
                        text: readout.pt2 ? (root.label2.length > 0 ? root.label2 + " " : "")
                                            + root.formatY(readout.pt2.y) : ""
                        color: Theme.text
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: readout.at ? Format.smartDateTime(readout.at.x) : ""
                        color: Theme.subtext
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }
            }
        }
    }
}
