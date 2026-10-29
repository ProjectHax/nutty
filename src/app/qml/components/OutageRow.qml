// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Layouts
import Nutty

// One on-battery episode in a list: when, how long, how low it went and
// what nutty did about it.
Item {
    id: root

    property string deviceName
    property var start
    property bool ongoing: false
    property real duration: 0
    property real startCharge: -1
    property real minCharge: -1
    property real minRuntime: -1
    property var actions: []
    property bool first: false
    property bool showDevice: true

    signal clicked()

    implicitHeight: layout.implicitHeight + 20
    implicitWidth: 480

    Rectangle {
        visible: !root.first
        width: parent.width
        height: 1
        color: Theme.divider
    }
    Rectangle {
        anchors.fill: parent
        anchors.topMargin: 2
        anchors.bottomMargin: 2
        radius: Theme.radius
        color: hover.hovered ? Theme.hover : "transparent"
    }
    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }
    TapHandler {
        onTapped: root.clicked()
    }

    RowLayout {
        id: layout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        spacing: 14

        Icon {
            Layout.preferredWidth: 22
            name: root.ongoing ? "plug-off" : "history"
            size: 18
            color: root.ongoing ? Theme.warning : Theme.subtext
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Text {
                Layout.fillWidth: true
                text: (root.showDevice && root.deviceName.length > 0 ? root.deviceName + " · " : "")
                      + Format.smartDateTime(root.start)
                color: Theme.text
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: {
                    const parts = []
                    if (root.startCharge >= 0 && root.minCharge >= 0)
                        parts.push(qsTr("%1 → %2").arg(Format.percent(root.startCharge)).arg(Format.percent(root.minCharge)))
                    if (root.minRuntime >= 0)
                        parts.push(qsTr("lowest runtime %1").arg(Format.duration(root.minRuntime)))
                    for (let i = 0; i < root.actions.length; ++i)
                        parts.push(root.actions[i].text)
                    return parts.join(" · ")
                }
                visible: text.length > 0
                color: Theme.subtext
                font.pixelSize: Theme.fontSizeSmall
                elide: Text.ElideRight
            }
        }

        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: durationLabel.implicitWidth + 18
            implicitHeight: 24
            radius: Theme.radiusSmall
            color: Theme.alpha(root.ongoing ? Theme.warning : Theme.text, root.ongoing ? 0.16 : 0.07)
            Text {
                id: durationLabel
                anchors.centerIn: parent
                text: root.ongoing ? qsTr("Ongoing · %1").arg(Format.durationShort(root.duration)) : Format.durationShort(root.duration)
                color: root.ongoing ? Theme.warning : Theme.text
                font.pixelSize: Theme.fontSizeSmall
                font.weight: Font.DemiBold
            }
        }
    }
}
