// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Layouts
import Nutty

// The first step of adding a device or a channel: a card per type, with its
// icon, name and a line about it. The cards of a row share its height.
GridLayout {
    id: root

    // [{ key, name, description, icon }]
    property var model: []

    signal picked(string key)

    Layout.fillWidth: true
    columns: width > 520 ? 2 : 1
    columnSpacing: 16
    rowSpacing: 16

    Repeater {
        model: root.model
        delegate: Card {
            required property var modelData
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Equal columns, whatever the text.
            Layout.preferredWidth: 1
            clickable: true
            padding: 16
            onClicked: root.picked(modelData.key)

            RowLayout {
                width: parent.width
                spacing: 14
                Icon {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 1
                    name: modelData.icon
                    size: 20
                    color: Theme.accent
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 4
                    Text {
                        Layout.fillWidth: true
                        text: modelData.name
                        color: Theme.text
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: modelData.description
                        color: Theme.subtext
                        font.pixelSize: Theme.fontSizeSmall
                        lineHeight: 1.15
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }
}
