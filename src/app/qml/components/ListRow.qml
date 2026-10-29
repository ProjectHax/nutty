// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Layouts
import Nutty

// One row of a list card: an icon, a title and an optional description (and
// anything in `details` under them), then the row's controls on the right.
// Settings, channels, rules and groups all use it, so rows line up from card
// to card: the same insets, icon column, minimum height and dividers.
//
//   Card {
//       padding: Theme.listPadding
//       ColumnLayout {
//           width: parent.width
//           spacing: 0
//           ListRow { divider: false; iconName: "bell"; title: "..."; Toggle {} }
//           ListRow { iconName: "mail"; title: "..."; IconButton {} }
//       }
//   }
Item {
    id: root

    property string iconName
    property color iconColor: Theme.subtext
    property string title
    property color titleColor: Theme.text
    property string description
    property bool elideTitle: false
    property bool elideDescription: false
    // A line above the row, between it and the one before.
    property bool divider: true
    // The controls go under the text instead of beside it (narrow windows).
    property bool stacked: false
    default property alias controls: controlRow.data
    property alias details: detailColumn.data

    Layout.fillWidth: true
    implicitWidth: 400
    implicitHeight: Math.max(Theme.rowHeight, layout.implicitHeight + 24)

    Rectangle {
        visible: root.divider
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.rowInset
        anchors.rightMargin: Theme.rowInset
        height: 1
        color: Theme.divider
    }

    GridLayout {
        id: layout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.rowInset
        anchors.rightMargin: Theme.rowInset
        anchors.verticalCenter: parent.verticalCenter
        columns: root.stacked ? 1 : 2
        columnSpacing: 14
        rowSpacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 14

            Icon {
                visible: root.iconName.length > 0 && !root.stacked
                Layout.preferredWidth: 22
                Layout.alignment: Qt.AlignTop
                // Centred on the title and the first line under it, so it
                // stays put when a description runs to several lines.
                Layout.topMargin: Math.max(0, (textColumn.leadHeight - size) / 2)
                name: root.iconName
                size: 20
                color: root.iconColor
            }

            ColumnLayout {
                id: textColumn
                readonly property real leadHeight: titleText.implicitHeight
                    + (descriptionText.visible
                       ? spacing + descriptionText.implicitHeight / Math.max(1, descriptionText.lineCount)
                       : 0)
                Layout.fillWidth: true
                spacing: 2

                Text {
                    id: titleText
                    Layout.fillWidth: true
                    text: root.title
                    color: root.titleColor
                    font.weight: Font.DemiBold
                    wrapMode: root.elideTitle ? Text.NoWrap : Text.Wrap
                    elide: root.elideTitle ? Text.ElideRight : Text.ElideNone
                }
                Text {
                    id: descriptionText
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: root.description
                    color: Theme.subtext
                    font.pixelSize: Theme.fontSizeSmall
                    wrapMode: root.elideDescription ? Text.NoWrap : Text.Wrap
                    elide: root.elideDescription ? Text.ElideRight : Text.ElideNone
                    lineHeight: 1.15
                }
                ColumnLayout {
                    id: detailColumn
                    Layout.fillWidth: true
                    visible: children.length > 0
                    spacing: 2
                }
            }
        }

        RowLayout {
            id: controlRow
            Layout.alignment: root.stacked ? Qt.AlignLeft : (Qt.AlignRight | Qt.AlignVCenter)
            visible: children.length > 0
            spacing: 14
        }
    }
}
