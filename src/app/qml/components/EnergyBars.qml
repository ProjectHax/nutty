// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import Nutty

// Energy per day as a pair of bars: what went in (with the part from solar
// in its own colour) and what went out, with the day's numbers on hover.
//
//   days: Series.energy(...).days, oldest first
Item {
    id: root

    // Days run left to right in every language.
    LayoutMirroring.enabled: false
    LayoutMirroring.childrenInherit: true

    property var days: []
    property real maxWh: 0
    property color inColor: Theme.success
    property color solarColor: Theme.warning
    property color outColor: Theme.accent

    readonly property real leftPad: 54
    readonly property real bottomPad: 22
    readonly property real topPad: 8
    // A round number at or above the biggest day.
    readonly property real ceiling: {
        const m = Math.max(maxWh, 1)
        const p = Math.pow(10, Math.floor(Math.log(m) / Math.LN10))
        for (const f of [1, 2, 2.5, 5, 10]) {
            if (f * p >= m)
                return f * p
        }
        return 10 * p
    }
    readonly property real slot: days.length > 0 ? plot.width / days.length : 0

    implicitWidth: 480
    implicitHeight: 200

    // Y grid + labels.
    Repeater {
        model: 3
        Item {
            required property int index
            readonly property real value: root.ceiling * index / 2
            y: root.topPad + plot.height * (1 - index / 2)
            width: root.width
            Rectangle {
                x: root.leftPad
                width: plot.width
                height: 1
                color: Theme.divider
            }
            Text {
                visible: root.maxWh > 0
                anchors.right: parent.left
                anchors.rightMargin: -root.leftPad + 8
                anchors.verticalCenter: parent.top
                text: Format.energy(parent.value)
                color: Theme.faint
                font.pixelSize: Theme.fontSizeSmall
            }
        }
    }

    Item {
        id: plot
        x: root.leftPad
        y: root.topPad
        width: root.width - root.leftPad - 6
        height: root.height - root.topPad - root.bottomPad

        Repeater {
            model: root.days
            Item {
                id: day
                required property var modelData
                required property int index
                readonly property bool hovered: hover.hovered && hover.index === index
                readonly property real barWidth: Math.max(2, Math.min(14, root.slot * 0.32))
                x: index * root.slot
                width: root.slot
                height: plot.height

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -1
                    radius: Theme.radiusSmall
                    color: day.hovered ? Theme.hover : "transparent"
                }
                // In, with solar at its foot.
                Rectangle {
                    x: day.width / 2 - day.barWidth - 1
                    width: day.barWidth
                    height: plot.height * Math.min(1, day.modelData.whIn / root.ceiling)
                    y: plot.height - height
                    radius: Math.min(3, width / 2)
                    color: root.inColor
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: parent.height * Math.min(1, day.modelData.whSolar / Math.max(1, day.modelData.whIn))
                        radius: parent.radius
                        color: root.solarColor
                    }
                }
                Rectangle {
                    x: day.width / 2 + 1
                    width: day.barWidth
                    height: plot.height * Math.min(1, day.modelData.whOut / root.ceiling)
                    y: plot.height - height
                    radius: Math.min(3, width / 2)
                    color: root.outColor
                }
                Text {
                    // Every day when there's room, else every few, always today.
                    readonly property int every: Math.max(1, Math.ceil(34 / Math.max(1, root.slot)))
                    visible: day.index === root.days.length - 1 || (root.days.length - 1 - day.index) % every === 0
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: plot.height + 5
                    text: day.index === root.days.length - 1 ? qsTr("Today")
                        : new Date(day.modelData.x).toLocaleDateString(Qt.locale(), root.days.length > 8 ? "d" : "ddd")
                    color: Theme.faint
                    font.pixelSize: Theme.fontSizeSmall
                }
            }
        }

        HoverHandler {
            id: hover
            readonly property int index: root.slot > 0 ? Math.floor(point.position.x / root.slot) : -1
        }

        Rectangle {
            id: tip
            readonly property var d: hover.hovered && hover.index >= 0 && hover.index < root.days.length ? root.days[hover.index] : null
            readonly property real ax: (hover.index + 0.5) * root.slot
            visible: d !== null
            x: Math.max(0, Math.min(plot.width - width, ax + 14 + width > plot.width ? ax - 14 - width : ax + 14))
            y: 4
            width: tipColumn.implicitWidth + 16
            height: tipColumn.implicitHeight + 10
            radius: Theme.radiusSmall
            color: Theme.elevated
            border.width: 1
            border.color: Theme.border
            Column {
                id: tipColumn
                anchors.centerIn: parent
                spacing: 1
                Text {
                    text: tip.d ? new Date(tip.d.x).toLocaleDateString(Qt.locale(), Locale.ShortFormat) : ""
                    color: Theme.subtext
                    font.pixelSize: Theme.fontSizeSmall
                }
                Text {
                    text: tip.d ? qsTr("In %1").arg(Format.energy(tip.d.whIn)) : ""
                    color: Theme.text
                    font.weight: Font.DemiBold
                }
                Text {
                    visible: tip.d !== null && tip.d.whSolar > 0
                    text: tip.d ? qsTr("Solar %1").arg(Format.energy(tip.d.whSolar)) : ""
                    color: Theme.text
                }
                Text {
                    text: tip.d ? qsTr("Out %1").arg(Format.energy(tip.d.whOut)) : ""
                    color: Theme.text
                    font.weight: Font.DemiBold
                }
            }
        }
    }
}
