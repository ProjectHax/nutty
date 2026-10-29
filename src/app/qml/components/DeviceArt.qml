// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import Nutty

// A picture of the device: the user's own image when `imageSource` is set,
// otherwise bundled artwork for `kind` (which carries its own floor
// shadow).
//   kind: TowerUps, RackUps, PowerStation, HomeBattery, Laptop, Generic
Item {
    id: root

    property string kind: "Generic"
    property url imageSource
    property bool dimmed: false

    readonly property string artName: {
        switch (kind) {
        case "TowerUps": return "tower-ups"
        case "RackUps": return "rack-ups"
        case "PowerStation": return "power-station"
        case "HomeBattery": return "home-battery"
        case "Laptop": return "laptop"
        }
        return "generic-battery"
    }

    implicitWidth: 96
    implicitHeight: 96

    Image {
        id: picture
        anchors.fill: parent
        anchors.margins: parent.width * 0.04
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        asynchronous: false
        source: root.imageSource.toString().length > 0 ? root.imageSource : "image://art/" + root.artName
        sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
        opacity: root.dimmed ? 0.45 : 1
        Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }
    }
}
