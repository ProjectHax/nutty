// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Layouts
import Nutty

// The headline at the top of the home page: "All systems on line",
// "Office UPS is on battery", ... `level` is ok, info, warning or critical.
Item {
    id: root

    property string level: "ok"
    property string title
    property string subtitle
    property string iconName: level === "ok" ? "check" : level === "info" ? "info" : level === "warning" ? "bolt" : "warning"
    property string actionText
    readonly property color tint: level === "ok" ? Theme.success : level === "info" ? Theme.info : level === "warning" ? Theme.warning : Theme.critical

    signal actionClicked()

    implicitWidth: 560
    implicitHeight: Math.max(88, layout.implicitHeight + 32)

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusLarge
        color: Theme.mix(Theme.card, root.tint, Theme.dark ? 0.10 : 0.06)
        border.width: 1
        border.color: Theme.alpha(root.tint, Theme.dark ? 0.35 : 0.28)
    }

    RowLayout {
        id: layout
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 18
        spacing: 16

        Icon {
            Layout.alignment: Qt.AlignVCenter
            name: root.iconName
            size: 28
            color: root.tint
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 2
            Text {
                Layout.fillWidth: true
                text: root.title
                color: Theme.text
                font.pixelSize: Theme.fontSizeTitle
                font.weight: Font.Bold
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                visible: text.length > 0
                text: root.subtitle
                color: Theme.subtext
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
        }

        PillButton {
            Layout.alignment: Qt.AlignVCenter
            visible: root.actionText.length > 0
            text: root.actionText
            tint: root.level === "ok" || root.level === "info" ? Theme.accent : root.tint
            onClicked: root.actionClicked()
        }
    }
}
