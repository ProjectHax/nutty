// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import Nutty

// A modal panel over the window for editors and confirmations: a title,
// a scrolling body and a row of buttons at the bottom (`footer` items,
// laid out right to left as given).
T.Popup {
    id: root

    property string title
    property string subtitle
    property string iconName
    property color iconColor: Theme.accent
    property real preferredWidth: 560
    default property alias body: bodyColumn.data
    property alias footerItems: footerRow.data
    property alias leadingFooterItems: leadingRow.data

    parent: T.Overlay.overlay
    modal: true
    dim: true
    focus: true
    closePolicy: T.Popup.CloseOnEscape
    anchors.centerIn: parent
    // T.Popup has no implicit size of its own (a style normally adds one),
    // so size the sheet to its content and let the body scroll past the cap.
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    width: Math.min(preferredWidth, (parent ? parent.width : 800) - 32)
    height: Math.min(implicitHeight, (parent ? parent.height : 600) - 32)
    padding: 0

    T.Overlay.modal: Rectangle {
        color: Theme.alpha("#000000", Theme.dark ? 0.5 : 0.28)
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationNormal; easing.type: Easing.OutCubic }
        NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: Theme.durationNormal; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast }
    }

    background: Item {
        SoftShadow {
            anchors.fill: parent
            radius: Theme.radiusLarge
            blur: 18
            offsetY: 6
            strength: Theme.dark ? 0.8 : 0.6
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusLarge
            color: Theme.elevated
            border.width: 1
            border.color: Theme.border
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            id: header
            Layout.fillWidth: true
            Layout.margins: 22
            Layout.bottomMargin: 14
            spacing: 12

            Icon {
                visible: root.iconName.length > 0
                Layout.alignment: Qt.AlignVCenter
                name: root.iconName
                size: 20
                color: root.iconColor
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    Layout.fillWidth: true
                    text: root.title
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeTitle - 2
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: root.subtitle
                    color: Theme.subtext
                    font.pixelSize: Theme.fontSizeSmall
                    wrapMode: Text.Wrap
                }
            }
            IconButton {
                Layout.alignment: Qt.AlignTop
                iconName: "close"
                toolTip: qsTr("Close")
                onClicked: root.close()
            }
        }

        Flickable {
            id: scroller
            Layout.fillWidth: true
            Layout.fillHeight: true
            implicitHeight: bodyColumn.implicitHeight + 24
            contentWidth: width
            contentHeight: bodyColumn.implicitHeight + 24
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            // Inset from the clip edge so hovered cards can lift, with room
            // above the footer.
            ColumnLayout {
                id: bodyColumn
                x: 22
                y: 4
                width: scroller.width - 44
                spacing: 14
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
            visible: footerRow.children.length > 0 || leadingRow.children.length > 0
        }

        RowLayout {
            id: footer
            Layout.fillWidth: true
            Layout.margins: 16
            Layout.leftMargin: 22
            Layout.rightMargin: 22
            visible: footerRow.children.length > 0 || leadingRow.children.length > 0
            spacing: 10
            Row {
                id: leadingRow
                spacing: 8
            }
            Item { Layout.fillWidth: true }
            Row {
                id: footerRow
                spacing: 10
                layoutDirection: Qt.RightToLeft
            }
        }
    }
}
