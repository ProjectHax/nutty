// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import Nutty

// A rounded text button for prominent actions (Cancel shutdown, Add device).
//   kind: "primary" (filled with `tint`), "secondary" (subtle fill), "ghost"
T.AbstractButton {
    id: control

    property string kind: "primary"
    property color tint: Theme.accent
    property string iconName
    property bool large: false
    property string toolTip

    readonly property color fg: kind === "primary" ? (tint === Theme.accent ? Theme.accentText : "#ffffff")
                                                   : (kind === "ghost" ? tint : Theme.text)

    implicitHeight: large ? 44 : Theme.controlHeight
    implicitWidth: Math.max(implicitHeight * 2.2, row.implicitWidth + (large ? 40 : 28))
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Button
    Accessible.name: text
    opacity: enabled ? 1 : 0.5

    ToolTip.visible: hovered && toolTip.length > 0
    ToolTip.text: toolTip
    ToolTip.delay: 600

    contentItem: Item {
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 8
            Icon {
                visible: control.iconName.length > 0
                anchors.verticalCenter: parent.verticalCenter
                name: control.iconName
                size: control.large ? 18 : 16
                color: control.fg
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: control.text
                color: control.fg
                font.pixelSize: control.large ? Theme.fontSizeLarge : Theme.fontSize
                font.weight: Font.DemiBold
            }
        }
    }

    background: Rectangle {
        radius: Theme.radius
        color: {
            if (control.kind === "primary") {
                const base = control.tint
                return control.down ? Qt.darker(base, 1.18) : control.hovered ? Qt.lighter(base, 1.08) : base
            }
            if (control.kind === "secondary")
                return control.down ? Theme.alpha(Theme.text, 0.16) : control.hovered ? Theme.alpha(Theme.text, 0.12) : Theme.alpha(Theme.text, 0.07)
            return control.down ? Theme.alpha(control.tint, 0.18) : control.hovered ? Theme.alpha(control.tint, 0.10) : "transparent"
        }
        border.width: control.visualFocus ? 2 : 0
        border.color: Theme.alpha(Theme.accent, 0.7)
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    }
}
