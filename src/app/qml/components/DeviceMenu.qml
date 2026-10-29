// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import Nutty

// The "more" menu of a row in a list: reorder and remove.
PopupMenu {
    id: root

    property bool first: false
    property bool last: false
    property bool movable: true

    signal moveUp()
    signal moveDown()
    signal remove()

    MenuEntry {
        visible: root.movable
        enabled: !root.first
        iconName: "chevron-up"
        text: qsTr("Move up")
        onTriggered: root.moveUp()
    }
    MenuEntry {
        visible: root.movable
        enabled: !root.last
        iconName: "chevron-down"
        text: qsTr("Move down")
        onTriggered: root.moveDown()
    }
    MenuEntry {
        danger: true
        iconName: "trash"
        text: qsTr("Remove…")
        onTriggered: root.remove()
    }
}
