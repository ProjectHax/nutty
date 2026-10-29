// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import Nutty

// The always-on-top alert shown while a power action counts down. Closing
// it only hides the alert; the countdown carries on until cancelled.
Window {
    id: win

    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true

    property alias action: panel.action
    property alias actionText: panel.actionText
    property alias deviceName: panel.deviceName
    property alias reason: panel.reason
    property alias remaining: panel.remaining
    property alias total: panel.total
    property alias canRunNow: panel.canRunNow
    property alias cancellable: panel.cancellable
    property alias note: panel.note

    signal cancelRequested()
    signal runNowRequested()
    signal dismissed()

    title: qsTr("Power action pending")
    width: 400
    height: Math.max(440, panel.implicitHeight + 2)
    minimumWidth: 360
    minimumHeight: height
    flags: (Theme.frameless ? Qt.FramelessWindowHint : 0) | Qt.Dialog | Qt.WindowStaysOnTopHint
    color: Theme.frameless ? "transparent" : Theme.window
    modality: Qt.NonModal

    onClosing: win.dismissed()

    WindowFrame {
        id: frame
        window: win
        resizable: false
        color: Theme.elevated

        DragHandler {
            target: null
            grabPermissions: PointerHandler.CanTakeOverFromAnything
            onActiveChanged: if (active) win.startSystemMove()
        }

        IconButton {
            z: 1
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 10
            visible: Theme.frameless
            iconName: "close"
            toolTip: qsTr("Hide")
            onClicked: win.close()
        }

        CountdownPanel {
            id: panel
            anchors.fill: parent
            onCancelRequested: win.cancelRequested()
            onRunNowRequested: win.runNowRequested()
        }
    }
}
