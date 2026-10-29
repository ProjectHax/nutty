// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Templates as T
import Nutty

// One item of a PopupMenu, with an optional icon.
T.MenuItem {
    id: control

    property string iconName
    property bool danger: false

    implicitWidth: 220
    implicitHeight: visible ? 34 : 0
    leftPadding: 10
    rightPadding: 10
    hoverEnabled: true

    contentItem: Row {
        spacing: 10
        Icon {
            anchors.verticalCenter: parent.verticalCenter
            visible: control.iconName.length > 0
            name: control.iconName
            size: 15
            color: control.danger ? Theme.critical : Theme.subtext
            opacity: control.enabled ? 1 : 0.5
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            color: control.danger ? Theme.critical : control.enabled ? Theme.text : Theme.faint
            font.pixelSize: Theme.fontSize
        }
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: control.highlighted || control.hovered ? (control.danger ? Theme.alpha(Theme.critical, 0.12) : Theme.hover) : "transparent"
    }
}
