// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import Nutty

// A soft two-layer shadow for a rounded box. Give it the box's geometry
// (usually `anchors.fill: <card>`); it draws outside those bounds. Falls back
// to a faint offset outline when shader effects are off.
Item {
    id: root

    property real radius: Theme.radiusLarge
    property real blur: 22
    property real offsetY: 8
    property real strength: 1.0
    property color color: Theme.shadow

    readonly property real margin: blur + Math.abs(offsetY) + 4

    Loader {
        anchors.fill: parent
        anchors.margins: -root.margin
        active: Theme.effectsEnabled && root.strength > 0
        sourceComponent: ShaderEffect {
            property color color: root.color
            property size itemSize: Qt.size(width, height)
            property size boxSize: Qt.size(root.width, root.height)
            property real radius: root.radius
            property real blur: root.blur
            property real offsetY: root.offsetY
            property real strength: root.strength
            fragmentShader: "qrc:/nutty/shaders/softshadow.frag.qsb"
        }
    }

    Rectangle {
        visible: !Theme.effectsEnabled && root.strength > 0
        anchors.fill: parent
        anchors.topMargin: 1
        anchors.bottomMargin: -2
        radius: root.radius
        color: Theme.alpha(root.color, Math.min(1, root.color.a * 0.6 * root.strength))
    }
}
