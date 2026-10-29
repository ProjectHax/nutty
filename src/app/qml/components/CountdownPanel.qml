// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Layouts
import Nutty

// The content of the "about to shut down" alert: a big ring counting down,
// what will happen, why, and a Cancel button.
//   action: shutdown, hibernate, sleep, command
Item {
    id: root

    property string action: "shutdown"
    property string actionText                  // overrides the title
    property string deviceName
    property string reason                      // e.g. "Battery below 20 %"
    property int remaining: 60                  // seconds
    property int total: 60
    property bool canRunNow: true
    property bool cancellable: true             // false for forced shutdowns
    property string note                        // overrides the hint under the title

    readonly property bool urgent: remaining <= 10
    readonly property color tint: action === "sleep" || action === "command" ? Theme.warning : Theme.critical

    signal cancelRequested()
    signal runNowRequested()

    function titleText() {
        if (actionText.length > 0)
            return actionText
        switch (action) {
        case "hibernate": return qsTr("Hibernating soon")
        case "sleep": return qsTr("Going to sleep soon")
        case "command": return qsTr("Running your command soon")
        }
        return qsTr("Shutting down soon")
    }

    function clock(s) {
        s = Math.max(0, s)
        const m = Math.floor(s / 60)
        const r = s % 60
        return m > 0 ? m + ":" + (r < 10 ? "0" : "") + r : String(r)
    }

    implicitWidth: 380
    implicitHeight: layout.implicitHeight + 56

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: 28
        spacing: 18

        Item {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 188
            Layout.preferredHeight: 188

            ArcGauge {
                anchors.fill: parent
                sweepDegrees: 360
                thickness: 12
                value: root.remaining
                maximum: Math.max(1, root.total)
                colorEnd: root.tint
                colorStart: Qt.lighter(root.tint, 1.25)
                valueText: root.clock(root.remaining)
                unit: ""
                caption: root.remaining >= 60 ? qsTr("minutes left") : qsTr("seconds left")
                animationDuration: 1000
                easingType: Easing.Linear
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: root.titleText()
                color: Theme.text
                font.pixelSize: Theme.fontSizeTitle
                font.weight: Font.Bold
                wrapMode: Text.Wrap
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                visible: text.length > 0
                textFormat: Text.PlainText
                text: root.deviceName.length > 0 && root.reason.length > 0 ? qsTr("%1 · %2").arg(root.deviceName).arg(root.reason)
                                                                        : root.deviceName + root.reason
                color: Theme.subtext
                wrapMode: Text.Wrap
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: root.note.length > 0 ? root.note
                    : root.cancellable ? qsTr("Save your work. Cancel to keep this computer running.")
                    : qsTr("The UPS asked for a forced shutdown. Save your work now.")
                color: Theme.faint
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.Wrap
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 10
            PillButton {
                Layout.fillWidth: true
                visible: root.canRunNow
                kind: "secondary"
                large: true
                text: qsTr("Do it now")
                onClicked: root.runNowRequested()
            }
            PillButton {
                objectName: "cancelButton"
                Layout.fillWidth: true
                visible: root.cancellable
                kind: "primary"
                large: true
                iconName: "close"
                text: qsTr("Cancel")
                focus: true
                onClicked: root.cancelRequested()
            }
        }
    }
}
