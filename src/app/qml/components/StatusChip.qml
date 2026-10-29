// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import Nutty

// A pill showing a device's state. `status` is one of:
//   online, charging, onBattery, lowBattery, fsd, bypass, overload,
//   replaceBattery, offline, stale, error, unknown
// The dot pings while the device is on battery or worse.
Rectangle {
    id: root

    property string status: "unknown"
    property string text: ""
    readonly property bool alarming: status === "onBattery" || status === "lowBattery" || status === "fsd"
    readonly property color tint: Theme.stateColor(status)
    readonly property color labelColor: Theme.dark ? Qt.lighter(tint, 1.12) : Qt.darker(tint, 1.28)

    function label(s) {
        switch (s) {
        case "online": return qsTr("Online")
        case "charging": return qsTr("Charging")
        case "onBattery": return qsTr("On battery")
        case "lowBattery": return qsTr("Low battery")
        case "fsd": return qsTr("Forced shutdown")
        case "bypass": return qsTr("Bypass")
        case "overload": return qsTr("Overload")
        case "replaceBattery": return qsTr("Replace battery")
        case "offline": return qsTr("Offline")
        case "stale": return qsTr("No recent data")
        case "error": return qsTr("Error")
        }
        return qsTr("Unknown")
    }

    implicitHeight: 24
    implicitWidth: row.implicitWidth + 20
    radius: Theme.radiusSmall
    color: Theme.alpha(tint, Theme.dark ? 0.18 : 0.12)
    Accessible.role: Accessible.StaticText
    Accessible.name: row.children[1].text

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6

        Item {
            width: 8
            height: 8
            anchors.verticalCenter: parent.verticalCenter

            Rectangle {
                id: ping
                anchors.centerIn: parent
                width: 8
                height: 8
                radius: 4
                color: root.tint
                opacity: 0
                visible: root.alarming && Theme.animationsEnabled

                SequentialAnimation on scale {
                    running: ping.visible
                    loops: Animation.Infinite
                    NumberAnimation { from: 1; to: 2.6; duration: 1400; easing.type: Easing.OutCubic }
                    PauseAnimation { duration: 400 }
                }
                SequentialAnimation on opacity {
                    running: ping.visible
                    loops: Animation.Infinite
                    NumberAnimation { from: 0.55; to: 0; duration: 1400; easing.type: Easing.OutCubic }
                    PauseAnimation { duration: 400 }
                }
            }

            Rectangle {
                anchors.fill: parent
                radius: 4
                color: root.tint
            }
        }

        Text {
            text: root.text.length > 0 ? root.text : root.label(root.status)
            color: root.labelColor
            font.pixelSize: Theme.fontSizeSmall
            font.weight: Font.DemiBold
        }
    }
}
