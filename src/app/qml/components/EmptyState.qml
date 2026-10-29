// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Layouts
import Nutty

// What a list shows when it has nothing in it yet.
ColumnLayout {
    id: root

    property string iconName: "info"
    property string title
    property string text
    property string actionText
    property string actionIcon: "plus"

    signal actionClicked()

    Layout.fillWidth: true
    spacing: 10

    Icon {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: 12
        name: root.iconName
        size: 32
        color: Theme.faint
    }
    Text {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        text: root.title
        color: Theme.text
        font.pixelSize: Theme.fontSizeLarge
        font.weight: Font.DemiBold
        wrapMode: Text.Wrap
    }
    Text {
        Layout.fillWidth: true
        Layout.maximumWidth: 460
        Layout.alignment: Qt.AlignHCenter
        visible: text.length > 0
        horizontalAlignment: Text.AlignHCenter
        text: root.text
        color: Theme.subtext
        wrapMode: Text.Wrap
        lineHeight: 1.15
    }
    PillButton {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: 4
        Layout.bottomMargin: 12
        visible: root.actionText.length > 0
        text: root.actionText
        iconName: root.actionIcon
        onClicked: root.actionClicked()
    }
}
