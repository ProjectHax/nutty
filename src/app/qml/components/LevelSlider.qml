// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Templates as T
import Nutty

// A slider for a setting the user confirms before it applies. It shows
// `value` (the setting as it is) until dragged, reports where it was let go
// with picked(), and goes back to `value` on reset() (after a "no").
// Arrow keys move it too; picked() follows once they stop.
T.Slider {
    id: control

    // What the setting is now; the slider snaps back to it.
    property real current: 0
    property var formatValue: function(v) { return String(Math.round(v)) }

    signal picked(real value)

    function reset() {
        value = current
    }

    onCurrentChanged: if (!pressed && !settle.running) value = current
    Component.onCompleted: value = current

    implicitWidth: 220
    implicitHeight: 28
    padding: 0
    live: true
    snapMode: T.Slider.SnapAlways
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: formatValue(value)

    onPressedChanged: {
        if (!pressed && value !== current)
            picked(value)
    }
    onMoved: {
        if (!pressed)
            settle.restart()
    }

    Timer {
        id: settle
        interval: 900
        onTriggered: {
            if (!control.pressed && control.value !== control.current)
                control.picked(control.value)
        }
    }

    background: Item {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: 6

        Rectangle {
            anchors.fill: parent
            radius: 3
            color: Theme.alpha(Theme.text, control.hovered ? 0.18 : 0.13)
        }
        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: 3
            color: control.enabled ? Theme.accent : Theme.alpha(Theme.accent, 0.4)
        }
        // Where the setting is now, while dragging away from it.
        Rectangle {
            readonly property real at: (control.current - control.from) / Math.max(1, control.to - control.from)
            visible: control.value !== control.current
            x: Math.max(0, Math.min(parent.width, at * parent.width)) - width / 2
            anchors.verticalCenter: parent.verticalCenter
            width: 2
            height: 12
            radius: 1
            color: Theme.subtext
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: 18
        height: 18
        radius: 9
        color: "#ffffff"
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? Theme.accent : Theme.alpha("#000000", 0.18)
        opacity: control.enabled ? 1 : 0.7
        scale: control.pressed ? 1.1 : 1
        Behavior on scale {
            enabled: Theme.animationsEnabled
            NumberAnimation { duration: Theme.durationFast }
        }
    }
}
