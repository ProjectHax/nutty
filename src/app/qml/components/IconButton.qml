// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import Nutty

// A compact icon-only button with a hover background and an optional tooltip.
T.AbstractButton {
    id: control

    property string iconName
    property int iconSize: 16
    property color iconColor: Theme.text
    property bool danger: false
    property bool round: false
    property string toolTip

    implicitWidth: 32
    implicitHeight: 32
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.role: Accessible.Button
    Accessible.name: toolTip.length > 0 ? toolTip : text

    ToolTip.visible: hovered && toolTip.length > 0
    ToolTip.text: toolTip
    ToolTip.delay: 600

    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: control.iconName
            size: control.iconSize
            color: control.danger && control.hovered ? "#ffffff" : control.iconColor
            opacity: control.enabled ? 1 : 0.4
        }
    }

    background: Rectangle {
        radius: control.round ? height / 2 : Theme.radiusSmall
        color: control.danger && control.hovered ? Theme.critical
             : control.down ? Theme.pressed
             : control.hovered ? Theme.hover : "transparent"
        border.width: control.visualFocus ? 2 : 0
        border.color: Theme.accent
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    }
}
