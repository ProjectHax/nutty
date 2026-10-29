// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Templates as T
import Nutty

// A checkbox with its label, for lists of options (event types, members).
T.CheckBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(28, implicitContentHeight + topPadding + bottomPadding)
    padding: 2
    leftPadding: 28
    spacing: 8
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    indicator: Rectangle {
        x: 2
        anchors.verticalCenter: parent.verticalCenter
        width: 18
        height: 18
        radius: Theme.radiusSmall
        color: control.checked ? Theme.accent : "transparent"
        border.width: control.checked ? 0 : 1.5
        border.color: control.hovered ? Theme.subtext : Theme.alpha(Theme.text, 0.35)
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        Icon {
            anchors.centerIn: parent
            visible: control.checked
            name: "check"
            size: 13
            color: Theme.accentText
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: Theme.radiusSmall + 3
            color: "transparent"
            visible: control.visualFocus
            border.width: 2
            border.color: Theme.alpha(Theme.accent, 0.6)
        }
    }

    contentItem: Text {
        text: control.text
        color: control.enabled ? Theme.text : Theme.faint
        font.pixelSize: Theme.fontSize
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
    }
}
