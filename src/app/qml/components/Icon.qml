// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import Nutty

// A monochrome UI icon from resources/icons, tinted to `color` and rendered
// at the screen's pixel density by the "icons" image provider.
Image {
    id: root

    property string name
    property color color: Theme.text
    property int size: 18

    function hex(c) {
        const s = c.toString()
        return s.charAt(0) === "#" ? s.substring(1) : s
    }

    width: size
    height: size
    sourceSize: Qt.size(Math.ceil(size * Screen.devicePixelRatio), Math.ceil(size * Screen.devicePixelRatio))
    source: name.length > 0 ? "image://icons/" + name + "/" + hex(color) : ""
    fillMode: Image.PreserveAspectFit
    // Sideways arrows point the other way in right-to-left languages.
    mirror: LayoutMirroring.enabled && (name === "chevron-left" || name === "chevron-right")
    smooth: true
    asynchronous: false
}
