// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Layouts
import Nutty

// The navigation rail on the left of the main window.
//
//   items:       [{ key, text, icon, badge? }, ...] shown at the top
//   footerItems: the same, pinned to the bottom (About, ...)
//   currentKey:  the selected page; activated(key) asks to change it
//
// In a frameless window the header doubles as a drag area, and
// `cornerRadius` rounds the outer corners to match the window.
Item {
    id: root

    property var items: []
    property var footerItems: []
    property string currentKey
    property bool compact: false
    property string title: "nutty"
    property string subtitle
    property real cornerRadius: 0
    property Window window: null
    property real headerHeight: 52
    property color color: Theme.surface

    signal activated(string key)

    implicitWidth: compact ? 64 : 224
    Behavior on implicitWidth {
        enabled: Theme.animationsEnabled
        NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic }
    }

    // Background: rounded on the outer (left) side only.
    Rectangle {
        anchors.fill: parent
        radius: root.cornerRadius
        color: root.color
    }
    Rectangle {
        anchors.right: parent.right
        width: Math.min(parent.width, root.cornerRadius + 1)
        height: parent.height
        visible: root.cornerRadius > 0 && root.color.a > 0.99
        color: root.color
    }
    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.divider
    }

    Item {
        id: header
        width: parent.width
        height: root.headerHeight

        DragHandler {
            target: null
            enabled: root.window !== null
            grabPermissions: PointerHandler.CanTakeOverFromAnything
            onActiveChanged: if (active && root.window) root.window.startSystemMove()
        }
        TapHandler {
            enabled: root.window !== null
            onDoubleTapped: {
                if (!root.window)
                    return
                if (root.window.visibility === Window.Maximized)
                    root.window.showNormal()
                else
                    root.window.showMaximized()
            }
        }

        Row {
            anchors.left: parent.left
            anchors.leftMargin: root.compact ? (parent.width - logo.width) / 2 : 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10

            Image {
                id: logo
                width: 30
                height: 30
                anchors.verticalCenter: parent.verticalCenter
                source: "qrc:/nutty/appicon/nutty.svg"
                sourceSize: Qt.size(Math.ceil(30 * Screen.devicePixelRatio), Math.ceil(30 * Screen.devicePixelRatio))
                smooth: true
                mipmap: true
            }
            Column {
                visible: !root.compact
                anchors.verticalCenter: parent.verticalCenter
                Text {
                    text: root.title
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeLarge
                    font.weight: Font.Bold
                }
                Text {
                    visible: root.subtitle.length > 0
                    text: root.subtitle
                    color: Theme.faint
                    font.pixelSize: Theme.fontSizeSmall
                }
            }
        }
    }

    // The selection pill slides to the current item.
    Rectangle {
        id: pill
        property Item target: null
        visible: target !== null
        x: target ? column.x + target.x : 0
        y: target ? column.y + target.y : 0
        width: target ? target.width : 0
        height: target ? target.height : 0
        radius: Theme.radius
        color: Theme.alpha(Theme.accent, Theme.dark ? 0.20 : 0.13)
        Behavior on y {
            enabled: Theme.animationsEnabled && pill.visible
            NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic }
        }

        Rectangle {
            x: Qt.application.layoutDirection === Qt.RightToLeft ? parent.width - width - 3 : 3
            anchors.verticalCenter: parent.verticalCenter
            width: 3
            height: parent.height * 0.5
            radius: 1.5
            color: Theme.accent
        }
    }

    ColumnLayout {
        id: column
        anchors.top: header.bottom
        anchors.topMargin: 8
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 10
        anchors.rightMargin: 11
        spacing: 2

        Repeater {
            model: root.items
            delegate: navDelegate
        }
        Item { Layout.fillHeight: true }
        Repeater {
            model: root.footerItems
            delegate: navDelegate
        }
    }

    Component {
        id: navDelegate
        NavItem {
            id: navItem
            required property var modelData
            objectName: "nav-" + modelData.key
            Layout.fillWidth: true
            text: modelData.text
            iconName: modelData.icon
            badge: modelData.badge !== undefined ? String(modelData.badge) : ""
            compact: root.compact
            selected: modelData.key === root.currentKey
            onSelectedChanged: if (selected) pill.target = navItem
            Component.onCompleted: if (selected) pill.target = navItem
            onClicked: root.activated(modelData.key)
        }
    }
}
