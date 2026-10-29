// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Layouts
import Nutty

// One entry in the activity timeline. `type` and `severity` are the
// EventType / Severity key names (OnBattery, PowerRestored, ...; Info,
// Warning, Critical).
Item {
    id: root

    property var timestamp            // QDateTime or ms since epoch
    property string type: "Test"
    property string severity: "Info"
    property string deviceName
    property string title
    property string message
    property bool showDevice: true
    property bool showDate: false
    property bool firstInGroup: false
    property bool lastInGroup: false
    readonly property bool hovered: hover.hovered

    readonly property bool positive: type === "PowerRestored" || type === "CommRestored" || type === "ActionCancelled"
    readonly property color tint: positive ? Theme.success
                                           : severity === "Critical" ? Theme.critical
                                           : severity === "Warning" ? Theme.warning
                                           : Theme.info

    function iconFor(t) {
        switch (t) {
        case "OnBattery": return "plug-off"
        case "PowerRestored": return "plug"
        case "LowBattery":
        case "ChargeBelow": return "battery-low"
        case "RuntimeBelow": return "clock"
        case "ForcedShutdown":
        case "UpsOff": return "power"
        case "ReplaceBattery": return "battery-alert"
        case "Overload": return "gauge"
        case "Bypass": return "bypass"
        case "Alarm": return "bell"
        case "CommLost": return "link-off"
        case "CommRestored": return "link"
        case "AuthFailed": return "lock"
        case "ActionScheduled": return "timer"
        case "ActionCancelled": return "close"
        case "ActionExecuted": return "check"
        case "ActionFailed": return "warning"
        case "SettingChanged": return "sliders"
        case "SettingFailed": return "warning"
        }
        return "bell"
    }

    implicitWidth: 420
    implicitHeight: Math.max(52, body.implicitHeight + 18)

    HoverHandler { id: hover }

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: -6
        anchors.rightMargin: -6
        radius: Theme.radius
        color: root.hovered ? Theme.hover : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    }

    // Timeline rail. x doesn't follow LayoutMirroring, so mirror it by hand.
    readonly property bool rtl: Qt.application.layoutDirection === Qt.RightToLeft

    Rectangle {
        x: root.rtl ? root.width - 17 : 15
        width: 2
        y: root.firstInGroup ? 26 : 0
        height: (root.lastInGroup ? 26 : root.height) - y
        visible: !(root.firstInGroup && root.lastInGroup)
        color: Theme.divider
    }

    Rectangle {
        id: badge
        x: root.rtl ? root.width - width : 0
        y: 10
        width: 32
        height: 32
        radius: 16
        color: Theme.mix(Theme.card, root.tint, Theme.dark ? 0.16 : 0.10)
        Icon {
            anchors.centerIn: parent
            name: root.iconFor(root.type)
            size: 16
            color: root.tint
        }
    }

    RowLayout {
        id: body
        anchors.left: badge.right
        anchors.leftMargin: 12
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: 9
        spacing: 12

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1
            Text {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: root.showDevice && root.deviceName.length > 0
                      ? qsTr("%1 · %2").arg(root.title).arg(root.deviceName) : root.title
                color: Theme.text
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                visible: text.length > 0
                textFormat: Text.PlainText
                text: root.message
                color: Theme.subtext
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }
        }

        Text {
            Layout.alignment: Qt.AlignTop
            text: root.showDate ? Format.smartDateTime(root.timestamp) : Format.time(root.timestamp)
            color: Theme.faint
            font.pixelSize: Theme.fontSizeSmall
        }
    }
}
