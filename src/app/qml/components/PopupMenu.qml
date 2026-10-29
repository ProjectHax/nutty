// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Templates as T
import Nutty

// A context menu in the app's style; fill it with MenuEntry items.
T.Menu {
    id: root

    implicitWidth: 220
    implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
    padding: 6
    margins: 8
    overlap: 1

    delegate: MenuEntry {}

    contentItem: ListView {
        implicitHeight: contentHeight
        model: root.contentModel
        interactive: false
        clip: true
        currentIndex: root.currentIndex
    }

    background: Rectangle {
        radius: Theme.radius
        color: Theme.elevated
        border.width: 1
        border.color: Theme.border
        SoftShadow {
            z: -1
            anchors.fill: parent
            radius: Theme.radius
            blur: 12
            offsetY: 4
            strength: Theme.dark ? 0.7 : 0.5
        }
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast }
    }
}
