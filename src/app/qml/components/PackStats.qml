// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Layouts
import Nutty

// One battery pack in numbers: its charge, health, cycles, temperature,
// what's flowing in or out, capacity and the spread of its cell voltages.
//
//   pack: one DeviceControls.details.packs entry
ColumnLayout {
    id: root

    property var pack: ({})

    function num(v) { return v !== undefined && v !== null }

    spacing: 12

    RowLayout {
        Layout.fillWidth: true
        spacing: 10
        Icon {
            name: "battery"
            size: 18
            color: Theme.subtext
        }
        Text {
            Layout.fillWidth: true
            text: root.pack.title || ""
            color: Theme.text
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Text {
            visible: root.num(root.pack.charge)
            text: root.num(root.pack.charge) ? Format.percent(root.pack.charge) : ""
            color: Theme.text
            font.pixelSize: Theme.fontSizeLarge
            font.weight: Font.Bold
        }
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 6
        radius: 3
        color: Theme.alpha(Theme.text, 0.08)
        visible: root.num(root.pack.charge)
        Rectangle {
            width: parent.width * Math.max(0, Math.min(1, (root.pack.charge || 0) / 100))
            height: parent.height
            radius: 3
            color: (root.pack.charge || 0) <= 15 ? Theme.critical : (root.pack.charge || 0) <= 35 ? Theme.warning : Theme.success
        }
    }

    GridLayout {
        Layout.fillWidth: true
        columns: Math.max(2, Math.min(4, Math.floor(width / 150)))
        columnSpacing: 16
        rowSpacing: 12

        Repeater {
            model: {
                const p = root.pack
                const list = []
                if (root.num(p.health))
                    list.push({ icon: "heart", label: qsTr("Health"), value: Format.percent(p.health) })
                if (root.num(p.cycles))
                    list.push({ icon: "refresh", label: qsTr("Cycles"), value: Number(p.cycles).toLocaleString(Qt.locale(), "f", 0) })
                if (root.num(p.temperature))
                    list.push({ icon: "thermometer", label: qsTr("Temperature"), value: Format.celsius(p.temperature) })
                if (root.num(p.watts)) {
                    // A few watts either way is the pack looking after itself.
                    const w = Math.abs(p.watts) <= 5 ? 0 : p.watts
                    list.push({ icon: w > 0 ? "battery-charging" : "battery",
                                label: w > 0 ? qsTr("Charging") : w < 0 ? qsTr("Discharging") : qsTr("Battery flow"),
                                value: w === 0 ? qsTr("Idle") : Format.watts(Math.abs(w)) })
                }
                if (root.num(p.voltage))
                    list.push({ icon: "bolt", label: qsTr("Voltage"), value: Format.volts(p.voltage) })
                if (root.num(p.storedWh))
                    list.push({ icon: "battery-full", label: qsTr("Stored"), value: Format.energy(p.storedWh) })
                if (root.num(p.fullCapacity) && root.num(p.designCapacity) && p.designCapacity > 0)
                    list.push({ icon: "gauge", label: qsTr("Capacity"),
                                value: qsTr("%1 of new").arg(Format.percent(100 * p.fullCapacity / p.designCapacity)) })
                if (root.num(p.cellMin) && root.num(p.cellMax))
                    list.push({ icon: "cells", label: qsTr("Cell spread"),
                                value: qsTr("%1 mV").arg(Math.round((p.cellMax - p.cellMin) * 1000)) })
                return list
            }
            delegate: RowLayout {
                id: stat
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                spacing: 8
                Icon {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 2
                    name: stat.modelData.icon
                    size: 14
                    color: Theme.faint
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text {
                        Layout.fillWidth: true
                        text: stat.modelData.label
                        color: Theme.faint
                        font.pixelSize: Theme.fontSizeSmall
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: stat.modelData.value
                        color: Theme.text
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
}
