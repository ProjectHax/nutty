// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Layouts
import Nutty

// One setting on a device: its icon, name and what it does, what's
// happening to it, and the switch, slider or list that changes it.
// Changing it only asks: requested() goes to the page, which confirms with
// the user before anything is sent, then calls reset() to show the setting
// as it really is again.
SettingRow {
    id: row

    // One entry of DeviceControls.controls.
    required property var control

    signal requested(real value)

    function reset() {
        slider.reset()
        dropdown.resync()
    }

    readonly property bool failed: control.result === "Failed" || control.result === "Unconfirmed"

    iconName: control.icon
    iconColor: failed ? Theme.critical : Theme.subtext
    title: control.title
    description: control.description
    enabled: control.enabled

    details: [
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 2
            visible: row.control.pending || row.control.result.length > 0 || !row.control.known
            spacing: 6

            Rectangle {
                visible: row.control.pending
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: 8
                implicitHeight: 8
                radius: 4
                color: Theme.accent
                SequentialAnimation on opacity {
                    running: row.control.pending && Theme.animationsEnabled
                    loops: Animation.Infinite
                    NumberAnimation { from: 1; to: 0.25; duration: 600 }
                    NumberAnimation { from: 0.25; to: 1; duration: 600 }
                }
            }
            Icon {
                visible: !row.control.pending && row.control.result.length > 0
                Layout.alignment: Qt.AlignTop
                name: row.failed ? "warning" : "check"
                size: 14
                color: row.failed ? Theme.critical : Theme.success
            }
            Text {
                Layout.fillWidth: true
                text: row.control.pending ? row.control.pendingText
                    : row.control.result.length > 0 ? row.control.message
                    : qsTr("The device doesn't report this setting.")
                color: row.control.pending ? Theme.accent
                     : row.failed ? Theme.critical
                     : row.control.result.length > 0 ? Theme.success : Theme.faint
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.Wrap
            }
        }
    ]

    // ------------------------------------------------------------ controls
    Text {
        visible: row.control.kind === "Level"
        Layout.minimumWidth: 64
        horizontalAlignment: Text.AlignRight
        text: slider.pressed || slider.value !== slider.current
              ? slider.formatValue(slider.value) : row.control.valueText
        color: row.enabled ? Theme.text : Theme.faint
        font.weight: Font.DemiBold
    }
    LevelSlider {
        id: slider
        visible: row.control.kind === "Level"
        Layout.preferredWidth: 200
        from: row.control.minimum
        to: Math.max(row.control.minimum + row.control.step, row.control.maximum)
        stepSize: row.control.step
        current: row.control.value
        formatValue: function(v) {
            const unit = row.control.unit
            const n = Number(v).toLocaleString(Qt.locale(), "f", 0)
            return unit === "%" ? qsTr("%1 %").arg(n) : unit.length > 0 ? n + " " + unit : n
        }
        onPicked: v => row.requested(v)
    }
    Toggle {
        visible: row.control.kind === "Toggle"
        checkable: false
        checked: row.control.value === 1
        Accessible.name: row.control.title
        onClicked: row.requested(checked ? 0 : 1)
    }
    Dropdown {
        id: dropdown
        visible: row.control.kind === "Choice"
        implicitWidth: 160
        options: row.control.options
        value: row.control.value
        onPicked: v => {
            if (v !== row.control.value)
                row.requested(v)
        }
    }
}
