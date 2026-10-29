// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Layouts
import Nutty

// A section title with an optional detail and trailing controls.
//   SectionHeader { text: qsTr("Devices"); detail: "3"; PillButton { ... } }
RowLayout {
    id: root

    property alias text: label.text
    property alias detail: detailLabel.text
    default property alias actions: trailing.data

    Layout.fillWidth: true
    Layout.topMargin: 8
    spacing: 10

    Text {
        id: label
        color: Theme.text
        font.pixelSize: Theme.fontSizeLarge + 1
        font.weight: Font.Bold
    }
    Text {
        id: detailLabel
        Layout.fillWidth: true
        color: Theme.faint
        elide: Text.ElideRight
    }
    Row {
        id: trailing
        spacing: 8
    }
}
