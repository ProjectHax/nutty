// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Layouts
import Nutty

// The client-side title bar for frameless Linux windows. Drag to move,
// double-click to maximize; the window buttons follow the desktop's
// button layout (e.g. "appmenu:minimize,maximize,close" or
// "close,minimize:"), in GNOME or Breeze style.
//
// Extra controls (search, refresh, ...) go in `actions` and sit just inside
// the window buttons.
Item {
    id: root

    property Window window: null
    property string title
    property bool showTitle: true
    property string style: Theme.platform === "kde" ? "breeze" : "gnome"
    property string buttonLayout: Theme.buttonLayout
    property bool showWindowButtons: Theme.frameless
    // A back button before the title, for going to the previous page.
    property bool showBack: false
    property string backToolTip: qsTr("Back")
    default property alias actions: actionRow.data

    signal back()

    readonly property bool maximized: window !== null && (window.visibility === Window.Maximized || window.visibility === Window.FullScreen)
    readonly property var leftButtons: parse(buttonLayout.split(":")[0] || "")
    readonly property var rightButtons: parse(buttonLayout.indexOf(":") >= 0 ? buttonLayout.split(":")[1] : buttonLayout)

    function parse(side) {
        return side.split(",").map(s => s.trim()).filter(s => s === "minimize" || s === "maximize" || s === "close")
    }

    function toggleMaximized() {
        if (!window)
            return
        if (maximized)
            window.showNormal()
        else
            window.showMaximized()
    }

    implicitHeight: 52
    implicitWidth: 400

    DragHandler {
        target: null
        enabled: root.window !== null
        grabPermissions: PointerHandler.CanTakeOverFromAnything
        onActiveChanged: if (active && root.window) root.window.startSystemMove()
    }
    TapHandler {
        enabled: root.window !== null
        gesturePolicy: TapHandler.DragThreshold
        onDoubleTapped: root.toggleMaximized()
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 12
        spacing: 8

        Row {
            visible: root.showWindowButtons && root.leftButtons.length > 0
            spacing: root.style === "breeze" ? 4 : 12
            Repeater {
                model: root.showWindowButtons ? root.leftButtons : []
                WindowButton {
                    required property string modelData
                    role: modelData
                    style: root.style
                    maximized: root.maximized
                    onClicked: root.windowAction(modelData)
                }
            }
        }

        // The back button sits on the title's line, centred on its capitals.
        Item {
            Layout.fillWidth: true
            visible: root.showTitle || root.showBack
            implicitHeight: root.showTitle ? titleText.implicitHeight : backButton.implicitHeight

            IconButton {
                id: backButton
                objectName: "backButton"
                visible: root.showBack
                y: root.showTitle ? Math.round(titleText.baselineOffset - titleMetrics.capHeight / 2 - height / 2) : 0
                iconName: "chevron-left"
                toolTip: root.backToolTip
                onClicked: root.back()
            }
            Text {
                id: titleText
                x: backButton.visible ? backButton.width + 8 : 0
                width: parent.width - x
                visible: root.showTitle
                text: root.title
                color: Theme.text
                font.pixelSize: Theme.fontSizeTitle
                font.weight: Font.Bold
                elide: Text.ElideRight
            }
            FontMetrics {
                id: titleMetrics
                // Mentions font so it's measured again when the font changes.
                readonly property real capHeight: font.pixelSize > 0 ? tightBoundingRect("H").height : 0
                font: titleText.font
            }
        }
        Item {
            Layout.fillWidth: !root.showTitle
        }

        Row {
            id: actionRow
            spacing: 4
            Layout.alignment: Qt.AlignVCenter
        }

        Row {
            Layout.leftMargin: 6
            visible: root.showWindowButtons && root.rightButtons.length > 0
            spacing: root.style === "breeze" ? 4 : 12
            Repeater {
                model: root.showWindowButtons ? root.rightButtons : []
                WindowButton {
                    required property string modelData
                    role: modelData
                    style: root.style
                    maximized: root.maximized
                    onClicked: root.windowAction(modelData)
                }
            }
        }
    }

    function windowAction(role) {
        if (!window)
            return
        if (role === "minimize")
            window.showMinimized()
        else if (role === "maximize")
            toggleMaximized()
        else if (role === "close")
            window.close()
    }
}
