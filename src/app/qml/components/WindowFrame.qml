// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import Nutty

// The body of a window. In a frameless window (Linux) it paints the
// rounded background and a hairline border, and adds the resize handles;
// when maximized, tiled to full screen or decorated natively it is a plain
// rectangle. The window itself must be transparent:
//
//   Window { color: "transparent"; WindowFrame { window: parent... } }
//
// Content should respect `radius` in its corners (Sidebar takes it as
// cornerRadius).
Item {
    id: root

    property Window window: null
    property bool frameless: Theme.frameless
    property color color: Theme.window
    property bool resizable: true
    default property alias content: body.data

    readonly property bool maximized: window !== null && (window.visibility === Window.Maximized || window.visibility === Window.FullScreen)
    readonly property real radius: frameless && !maximized ? Theme.radiusWindow : 0
    readonly property real borderWidth: frameless && !maximized ? 1 : 0

    anchors.fill: parent

    Rectangle {
        id: background
        anchors.fill: parent
        radius: root.radius
        color: root.color
    }

    Item {
        id: body
        anchors.fill: parent
        anchors.margins: root.borderWidth
    }

    // The border sits above the content so it stays crisp at the corners.
    Rectangle {
        anchors.fill: parent
        visible: root.borderWidth > 0
        radius: root.radius
        color: "transparent"
        border.width: root.borderWidth
        border.color: Theme.dark ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(0, 0, 0, 0.16)
    }

    ResizeFrame {
        window: root.window
        visible: root.frameless && root.resizable && active
    }
}
