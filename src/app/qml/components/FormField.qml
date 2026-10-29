// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Layouts
import Nutty

// A labelled form control: the label above, the control, then a help line.
//   FormField { label: qsTr("Name"); InputField { ... } }
ColumnLayout {
    id: root

    property string label
    property string help
    property string error
    default property alias control: slot.data

    Layout.fillWidth: true
    spacing: 6

    Text {
        Layout.fillWidth: true
        visible: root.label.length > 0
        text: root.label
        color: Theme.subtext
        font.pixelSize: Theme.fontSizeSmall
        font.weight: Font.DemiBold
        elide: Text.ElideRight
    }
    Item {
        id: slot
        Layout.fillWidth: true
        implicitHeight: children.length > 0 ? children[0].implicitHeight : 0
        // The control spans the field's width.
        onChildrenChanged: {
            for (let i = 0; i < children.length; ++i)
                children[i].width = Qt.binding(() => slot.width)
        }
    }
    Text {
        Layout.fillWidth: true
        visible: text.length > 0
        text: root.error.length > 0 ? root.error : root.help
        color: root.error.length > 0 ? Theme.critical : Theme.faint
        font.pixelSize: Theme.fontSizeSmall
        wrapMode: Text.Wrap
    }
}
