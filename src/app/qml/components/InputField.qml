// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Templates as T
import Nutty

// A single-line text input in the app's style. `secret` hides what's typed
// (with a button to reveal it); `suffix` shows a unit at the right edge.
T.TextField {
    id: control

    property bool secret: false
    property bool revealed: false
    property string suffix
    property bool invalid: false

    implicitWidth: 240
    implicitHeight: Theme.controlHeight
    leftPadding: 12
    rightPadding: 12 + (secret ? 28 : 0) + (suffixLabel.visible ? suffixLabel.implicitWidth + 6 : 0)
    verticalAlignment: TextInput.AlignVCenter
    color: Theme.text
    placeholderTextColor: Theme.faint
    selectionColor: Theme.alpha(Theme.accent, 0.35)
    selectedTextColor: Theme.text
    font.pixelSize: Theme.fontSize
    echoMode: secret && !revealed ? TextInput.Password : TextInput.Normal
    selectByMouse: true
    hoverEnabled: true

    // Qt 6.4's T.TextField has no placeholder item of its own.
    Text {
        x: control.leftPadding
        width: control.width - control.leftPadding - control.rightPadding
        anchors.verticalCenter: parent.verticalCenter
        visible: control.length === 0 && !control.preeditText
        text: control.placeholderText
        color: control.placeholderTextColor
        font: control.font
        elide: Text.ElideRight
    }

    Text {
        id: suffixLabel
        anchors.right: parent.right
        anchors.rightMargin: 12 + (control.secret ? 28 : 0)
        anchors.verticalCenter: parent.verticalCenter
        visible: control.suffix.length > 0
        text: control.suffix
        color: Theme.faint
        font.pixelSize: Theme.fontSizeSmall
    }

    IconButton {
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        width: 28
        height: 28
        visible: control.secret
        iconName: control.revealed ? "eye-off" : "eye"
        iconSize: 14
        iconColor: Theme.subtext
        toolTip: control.revealed ? qsTr("Hide") : qsTr("Show")
        onClicked: control.revealed = !control.revealed
    }

    background: Rectangle {
        radius: Theme.radiusSmall + 2
        color: control.enabled ? Theme.alpha(Theme.text, Theme.dark ? 0.06 : 0.04) : Theme.alpha(Theme.text, 0.02)
        border.width: control.activeFocus ? 2 : 1
        border.color: control.invalid ? Theme.critical
                    : control.activeFocus ? Theme.accent
                    : control.hovered ? Theme.alpha(Theme.text, 0.28) : Theme.border
        Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }
    }
}
