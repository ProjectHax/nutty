// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Templates as T
import Nutty

// An on/off switch. `text` (optional) sits to the left of the track.
T.Switch {
    id: control

    implicitWidth: Math.max(implicitIndicatorWidth + leftPadding + rightPadding,
                            text.length > 0 ? implicitContentWidth + leftPadding + rightPadding : 0)
    implicitHeight: Math.max(26, implicitContentHeight + topPadding + bottomPadding)
    padding: 0
    spacing: 10
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.CheckBox
    Accessible.name: text

    indicator: Rectangle {
        x: control.text.length > 0 ? control.width - width : 0
        anchors.verticalCenter: parent.verticalCenter
        implicitWidth: 42
        implicitHeight: 24
        width: 42
        height: 24
        radius: 12
        color: control.checked ? (control.enabled ? Theme.accent : Theme.alpha(Theme.accent, 0.4))
                               : Theme.alpha(Theme.text, control.hovered ? 0.22 : 0.16)
        border.width: control.visualFocus ? 2 : 0
        border.color: Theme.alpha(Theme.accent, 0.6)
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        Rectangle {
            x: control.checked ? parent.width - width - 3 : 3
            anchors.verticalCenter: parent.verticalCenter
            width: 18
            height: 18
            radius: 9
            color: "#ffffff"
            opacity: control.enabled ? 1 : 0.7
            Behavior on x {
                enabled: Theme.animationsEnabled
                NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic }
            }
        }
    }

    contentItem: Text {
        visible: control.text.length > 0
        rightPadding: control.text.length > 0 ? 42 + control.spacing : 0
        text: control.text
        color: control.enabled ? Theme.text : Theme.faint
        font.pixelSize: Theme.fontSize
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
