// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import Nutty

// A flat surface with a border. Hoverable cards tint under the pointer;
// clickable ones emit clicked().
//
//   Card { hoverable: true; ColumnLayout { ... } }
//
// The first child sets the implicit size (plus padding).
Item {
    id: root

    default property alias content: body.data
    property real padding: Theme.padding
    property real radius: Theme.radiusLarge
    property color color: Theme.card
    property bool hoverable: false
    property bool clickable: false
    readonly property bool hovered: hover.hovered
    readonly property bool highlighted: (hoverable || clickable) && hover.hovered

    signal clicked()

    implicitWidth: (body.children.length > 0 ? body.children[0].implicitWidth : 0) + padding * 2
    implicitHeight: (body.children.length > 0 ? body.children[0].implicitHeight : 0) + padding * 2

    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: root.highlighted ? Theme.cardHover : root.color
        border.width: 1
        border.color: Theme.border
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    }

    HoverHandler {
        id: hover
        enabled: root.hoverable || root.clickable
        cursorShape: root.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    // Takes the press for itself: a passive grab would let it through to
    // whatever is underneath, such as the page under an open sheet.
    TapHandler {
        enabled: root.clickable
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: root.clicked()
    }

    Item {
        id: body
        anchors.fill: parent
        anchors.margins: root.padding
    }
}
