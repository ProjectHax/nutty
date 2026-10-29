// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Layouts
import Nutty

// "Remove Rack UPS?" — asks once, then calls `onAccepted`.
Sheet {
    id: root

    property string message
    property string confirmText: qsTr("Remove")
    property bool destructive: true

    signal accepted()

    preferredWidth: 440
    iconName: destructive ? "trash" : "info"

    Text {
        Layout.fillWidth: true
        Layout.bottomMargin: 8
        text: root.message
        color: Theme.subtext
        wrapMode: Text.Wrap
        lineHeight: 1.15
    }

    footerItems: [
        PillButton {
            text: root.confirmText
            tint: root.destructive ? Theme.critical : Theme.accent
            onClicked: {
                root.accepted()
                root.close()
            }
        },
        PillButton {
            kind: "secondary"
            text: qsTr("Cancel")
            onClicked: root.close()
        }
    ]
}
