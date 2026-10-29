// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import Nutty

// A row of mutually exclusive choices with a sliding highlight.
//   options: [{ value, text, icon? }]; `value` is the selected one.
Item {
    id: root

    property var options: []
    property var value
    property bool compact: false

    signal activated(var value)

    readonly property int currentIndex: {
        for (let i = 0; i < options.length; ++i) {
            if (options[i].value === value)
                return i
        }
        return -1
    }

    implicitWidth: row.implicitWidth + 6
    implicitHeight: compact ? 30 : Theme.controlHeight
    Accessible.role: Accessible.PageTabList

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: Theme.alpha(Theme.text, Theme.dark ? 0.07 : 0.05)
        border.width: 1
        border.color: Theme.border
    }

    Rectangle {
        id: thumb
        readonly property Item target: root.currentIndex >= 0 && repeater.count > root.currentIndex ? repeater.itemAt(root.currentIndex) : null
        visible: target !== null
        x: target ? row.x + target.x : 0
        y: 3
        width: target ? target.width : 0
        height: parent.height - 6
        radius: Theme.radiusSmall
        color: Theme.dark ? Theme.alpha("#ffffff", 0.14) : "#ffffff"
        border.width: Theme.dark ? 0 : 1
        border.color: Theme.alpha("#000000", 0.08)
        Behavior on x {
            enabled: Theme.animationsEnabled
            NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic }
        }
        Behavior on width {
            enabled: Theme.animationsEnabled
            NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic }
        }
    }

    Row {
        id: row
        x: 3
        height: parent.height
        Repeater {
            id: repeater
            model: root.options
            delegate: Item {
                id: cell
                required property var modelData
                required property int index
                readonly property bool selected: index === root.currentIndex
                width: label.implicitWidth + (icon.visible ? icon.width + 6 : 0) + (root.compact ? 22 : 28)
                height: row.height
                Accessible.role: Accessible.PageTab
                Accessible.name: modelData.text

                Row {
                    anchors.centerIn: parent
                    spacing: 6
                    Icon {
                        id: icon
                        anchors.verticalCenter: parent.verticalCenter
                        visible: cell.modelData.icon !== undefined
                        name: cell.modelData.icon !== undefined ? cell.modelData.icon : ""
                        size: 14
                        color: cell.selected ? Theme.text : Theme.subtext
                    }
                    Text {
                        id: label
                        anchors.verticalCenter: parent.verticalCenter
                        text: cell.modelData.text
                        color: cell.selected ? Theme.text : Theme.subtext
                        font.pixelSize: root.compact ? Theme.fontSizeSmall : Theme.fontSize
                        font.weight: cell.selected ? Font.DemiBold : Font.Normal
                    }
                }
                TapHandler {
                    gesturePolicy: TapHandler.ReleaseWithinBounds  // keep the press from what's underneath
                    onTapped: root.activated(cell.modelData.value)
                }
                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                }
            }
        }
    }
}
