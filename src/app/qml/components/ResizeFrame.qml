// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import Nutty

// Invisible resize handles along the inside edges of a frameless window.
// Pressing one hands the resize to the window manager, which keeps it
// smooth and DPI-correct on both X11 and Wayland.
Item {
    id: root

    property Window window: null
    property int border: 6
    property int corner: 14
    readonly property bool active: window !== null && window.visibility === Window.Windowed

    anchors.fill: parent
    z: 1000
    visible: active

    component Handle: MouseArea {
        property int edges: 0
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        onPressed: {
            if (root.window)
                root.window.startSystemResize(edges)
        }
    }

    Handle { x: root.corner; width: parent.width - root.corner * 2; height: root.border; edges: Qt.TopEdge; cursorShape: Qt.SizeVerCursor }
    Handle { x: root.corner; y: parent.height - height; width: parent.width - root.corner * 2; height: root.border; edges: Qt.BottomEdge; cursorShape: Qt.SizeVerCursor }
    Handle { y: root.corner; width: root.border; height: parent.height - root.corner * 2; edges: Qt.LeftEdge; cursorShape: Qt.SizeHorCursor }
    Handle { x: parent.width - width; y: root.corner; width: root.border; height: parent.height - root.corner * 2; edges: Qt.RightEdge; cursorShape: Qt.SizeHorCursor }
    Handle { width: root.corner; height: root.corner; edges: Qt.TopEdge | Qt.LeftEdge; cursorShape: Qt.SizeFDiagCursor }
    Handle { x: parent.width - width; width: root.corner; height: root.corner; edges: Qt.TopEdge | Qt.RightEdge; cursorShape: Qt.SizeBDiagCursor }
    Handle { y: parent.height - height; width: root.corner; height: root.corner; edges: Qt.BottomEdge | Qt.LeftEdge; cursorShape: Qt.SizeBDiagCursor }
    Handle { x: parent.width - width; y: parent.height - height; width: root.corner; height: root.corner; edges: Qt.BottomEdge | Qt.RightEdge; cursorShape: Qt.SizeFDiagCursor }
}
