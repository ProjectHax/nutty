// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Layouts
import Nutty

// Where the power comes from and where it goes right now: a column of
// inputs and one of outputs, each row with its watts and a bar for its
// share of that side's total.
//
//   inputs / outputs: [{ id, title, icon, watts }]
GridLayout {
    id: root

    property var inputs: []
    property var outputs: []
    property bool dimmed: false

    columns: width > 560 ? 2 : 1
    columnSpacing: 32
    rowSpacing: 18

    component Side: ColumnLayout {
        id: side
        property string title
        property var rows: []
        property color tint
        readonly property real total: rows.reduce((sum, r) => sum + r.watts, 0)

        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: side.title
                color: Theme.subtext
                font.pixelSize: Theme.fontSizeSmall
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 0.6
            }
            Text {
                text: Format.watts(side.total)
                color: Theme.text
                font.pixelSize: Theme.fontSizeLarge
                font.weight: Font.DemiBold
            }
        }

        Repeater {
            model: side.rows
            delegate: ColumnLayout {
                id: flowRow
                required property var modelData
                readonly property bool idle: modelData.watts < 1
                Layout.fillWidth: true
                spacing: 5
                opacity: flowRow.idle || root.dimmed ? 0.55 : 1

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Icon {
                        name: flowRow.modelData.icon
                        size: 16
                        color: flowRow.idle ? Theme.faint : side.tint
                    }
                    Text {
                        Layout.fillWidth: true
                        text: flowRow.modelData.title
                        color: Theme.text
                        elide: Text.ElideRight
                    }
                    Text {
                        text: Format.watts(flowRow.modelData.watts)
                        color: flowRow.idle ? Theme.faint : Theme.text
                        font.weight: Font.DemiBold
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 26
                    implicitHeight: 4
                    radius: 2
                    color: Theme.alpha(Theme.text, 0.08)
                    Rectangle {
                        width: parent.width * (side.total > 0 ? Math.min(1, flowRow.modelData.watts / side.total) : 0)
                        height: parent.height
                        radius: 2
                        color: side.tint
                        Behavior on width {
                            enabled: Theme.animationsEnabled
                            NumberAnimation { duration: Theme.durationSlow; easing.type: Easing.OutCubic }
                        }
                    }
                }
            }
        }
    }

    Side {
        title: qsTr("Coming in")
        rows: root.inputs
        tint: Theme.success
    }
    Side {
        title: qsTr("Going out")
        rows: root.outputs
        tint: Theme.accent
    }
}
