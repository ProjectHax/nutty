// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import Nutty

// A sidebar entry: icon, label and an optional badge. The selection pill
// itself is drawn by Sidebar so it can slide between items.
T.AbstractButton {
    id: control

    property string iconName
    property bool selected: false
    property bool compact: false
    property string badge            // e.g. "2"; empty hides it
    property color badgeColor: Theme.critical

    implicitHeight: 40
    implicitWidth: compact ? 44 : 200
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.role: Accessible.PageTab
    Accessible.name: text
    Accessible.selected: selected

    ToolTip.visible: compact && hovered
    ToolTip.text: text
    ToolTip.delay: 400

    contentItem: Item {
        Icon {
            id: icon
            x: control.compact ? (parent.width - width) / 2 : control.mirrored ? parent.width - width - 12 : 12
            anchors.verticalCenter: parent.verticalCenter
            name: control.iconName
            size: 18
            color: control.selected ? Theme.accent : Theme.subtext
            Behavior on x { NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic } }
        }
        Text {
            anchors.left: icon.right
            anchors.leftMargin: 12
            anchors.right: badgePill.visible ? badgePill.left : parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            color: control.selected ? Theme.text : Theme.subtext
            font.weight: control.selected ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
            opacity: control.compact ? 0 : 1
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
        }
        Rectangle {
            id: badgePill
            visible: control.badge.length > 0
            anchors.verticalCenter: control.compact ? undefined : parent.verticalCenter
            anchors.right: control.compact ? undefined : parent.right
            anchors.rightMargin: 10
            x: !control.compact ? 0 : control.mirrored ? 2 : parent.width - width - 2
            y: control.compact ? 2 : 0
            height: control.compact ? 14 : 18
            width: Math.max(height, badgeText.implicitWidth + 10)
            radius: height / 2
            color: control.badgeColor
            Text {
                id: badgeText
                anchors.centerIn: parent
                text: control.badge
                color: "#ffffff"
                font.pixelSize: control.compact ? 9 : 11
                font.weight: Font.Bold
            }
        }
    }

    background: Rectangle {
        radius: Theme.radius
        color: control.selected ? "transparent" : control.down ? Theme.pressed : control.hovered ? Theme.hover : "transparent"
        border.width: control.visualFocus ? 2 : 0
        border.color: Theme.alpha(Theme.accent, 0.7)
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    }
}
