// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Layouts
import Nutty

// One monitored device on the home page: picture, state, charge, runtime,
// load and a short charge history. Unknown numbers are < 0.
Card {
    id: root

    property string name
    property string subtitle
    property string kind: "Generic"
    property url imageSource
    property real charge: -1          // %
    property real runtime: -1         // seconds
    property real load: -1            // %
    property real wattsIn: -1
    property real wattsOut: -1
    property string status: "unknown"
    property string statusText
    property bool charging: status === "charging"
    property bool stale: status === "stale"
    property bool offline: status === "offline" || status === "error"
    property var lastUpdated          // QDateTime or ms since epoch
    property var history: []          // recent charge values, oldest first

    readonly property color tint: Theme.stateColor(status)
    property var now: new Date()

    clickable: true
    padding: 18
    implicitWidth: 340
    implicitHeight: column.implicitHeight + padding * 2

    Accessible.role: Accessible.Button
    Accessible.name: name

    Timer {
        interval: 1000
        repeat: true
        running: root.visible && root.lastUpdated !== undefined && root.Window.active
        onTriggered: root.now = new Date()
    }

    ColumnLayout {
        id: column
        anchors.fill: parent
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 14

            DeviceArt {
                Layout.preferredWidth: 80
                Layout.preferredHeight: 80
                kind: root.kind
                imageSource: root.imageSource
                dimmed: root.offline
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Text {
                    Layout.fillWidth: true
                    text: root.name
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeLarge
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: root.subtitle
                    color: Theme.subtext
                    font.pixelSize: Theme.fontSizeSmall
                    elide: Text.ElideRight
                }
                StatusChip {
                    Layout.topMargin: 2
                    status: root.status
                    text: root.statusText
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 14

            // As wide as "100%" whatever the charge, so the bars of every
            // card start at the same place.
            Row {
                Layout.alignment: Qt.AlignBottom
                Layout.preferredWidth: widest.advanceWidth + percent.implicitWidth + spacing
                spacing: 2
                TextMetrics {
                    id: widest
                    font: chargeNumber.font
                    text: Number(100).toLocaleString(Qt.locale(), "f", 0)
                }
                AnimatedNumber {
                    id: chargeNumber
                    value: root.offline ? -1 : root.charge
                    color: root.offline ? Theme.faint : Theme.text
                    font.pixelSize: Theme.fontSizeDisplay
                    font.weight: Font.Bold
                }
                Text {
                    id: percent
                    anchors.baseline: chargeNumber.baseline
                    visible: !chargeNumber.unknown
                    text: "%"
                    color: Theme.subtext
                    font.pixelSize: Theme.fontSizeLarge
                    font.weight: Font.DemiBold
                }
            }

            BatteryVisual {
                Layout.fillWidth: true
                Layout.preferredHeight: 34
                Layout.alignment: Qt.AlignVCenter
                level: root.charge
                charging: root.charging
                offline: root.offline
                stale: root.stale
                showLabel: false
                showTip: false
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 10
            rowSpacing: 2

            Repeater {
                model: [
                    { icon: "clock", label: qsTr("Runtime"), value: Format.duration(root.runtime) },
                    { icon: "gauge", label: qsTr("Load"), value: Format.percent(root.load) },
                    { icon: root.wattsIn > 0 && root.wattsOut <= 0 ? "plug" : "bolt",
                      label: root.wattsIn > 0 && root.wattsOut <= 0 ? qsTr("Input") : qsTr("Output"),
                      value: Format.watts(root.wattsIn > 0 && root.wattsOut <= 0 ? root.wattsIn : root.wattsOut) }
                ]
                delegate: Item {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 40
                    Column {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Row {
                            spacing: 5
                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: modelData.icon
                                size: 12
                                color: Theme.faint
                            }
                            Text {
                                text: modelData.label
                                color: Theme.faint
                                font.pixelSize: Theme.fontSizeSmall
                            }
                        }
                        Text {
                            text: root.offline ? "—" : modelData.value
                            color: Theme.text
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }
        }

        // Always laid out, even before there is a history to draw, so
        // cards side by side keep the same height; it takes up any slack.
        Sparkline {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 36
            Layout.preferredHeight: 36
            values: root.history
            // Charge rarely leaves the top of the scale; zoom in a little.
            minValue: root.history && root.history.length > 0 ? Math.max(0, Math.floor((Math.min.apply(null, root.history) - 20) / 10) * 10) : 0
            maxValue: 100
            color: root.offline || root.stale ? Theme.faint : Theme.levelColor(root.charge)
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Icon {
                name: root.stale || root.offline ? "warning" : "refresh"
                size: 12
                color: root.stale || root.offline ? Theme.warning : Theme.faint
            }
            Text {
                Layout.fillWidth: true
                text: root.lastUpdated === undefined ? qsTr("Waiting for data")
                                                     : qsTr("Updated %1").arg(Format.relativeTime(root.lastUpdated, root.now))
                color: root.stale || root.offline ? Theme.warning : Theme.faint
                font.pixelSize: Theme.fontSizeSmall
                elide: Text.ElideRight
            }
            Icon {
                name: "chevron-right"
                size: 14
                color: root.hovered ? Theme.subtext : Theme.faint
            }
        }
    }
}
