// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Templates as T
import Nutty

// A minimize / maximize / close button for the client-side title bar.
//   role:  "minimize", "maximize", "close"
//   style: "gnome"  — round, softly filled buttons (Adwaita)
//          "breeze" — flat glyphs, a round highlight and a red close (KDE)
T.AbstractButton {
    id: control

    property string role: "close"
    property string style: "gnome"
    property bool maximized: false

    readonly property bool breeze: style === "breeze"
    readonly property bool isClose: role === "close"
    readonly property string glyph: role === "maximize" ? (maximized ? "restore" : "maximize") : role

    implicitWidth: breeze ? 28 : 24
    implicitHeight: implicitWidth
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    Accessible.role: Accessible.Button
    Accessible.name: role === "minimize" ? qsTr("Minimize") : role === "maximize" ? (maximized ? qsTr("Restore") : qsTr("Maximize")) : qsTr("Close")

    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: control.glyph
            size: control.breeze ? 14 : 12
            color: control.breeze && control.isClose && control.hovered ? "#ffffff"
                 : (Window.active ? Theme.text : Theme.subtext)
        }
    }

    background: Rectangle {
        radius: width / 2
        color: {
            if (control.breeze) {
                if (control.isClose && (control.hovered || control.down))
                    return control.down ? "#c0392b" : "#da4453"
                return control.down ? Theme.pressed : control.hovered ? Theme.hover : "transparent"
            }
            const base = Theme.alpha(Theme.text, Theme.dark ? 0.10 : 0.08)
            return control.down ? Theme.alpha(Theme.text, 0.22) : control.hovered ? Theme.alpha(Theme.text, 0.16) : base
        }
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    }
}
