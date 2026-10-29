// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Layouts
import Nutty

// Make or edit a group of devices, for rules about "any" or "all" of them.
Sheet {
    id: root

    property string groupId
    property string name
    property var members: []
    property string error

    function edit(group) {
        groupId = group ? group.id : ""
        name = group ? group.name : ""
        members = group ? group.members.slice() : []
        error = ""
        open()
    }

    function setMember(id, on) {
        const list = members.filter(m => m !== id)
        if (on)
            list.push(id)
        members = list
    }

    title: groupId.length > 0 ? qsTr("Edit group") : qsTr("New group")
    subtitle: qsTr("Rules can watch any or every device in a group, like the UPSes that feed one rack.")
    iconName: "devices"
    preferredWidth: 520

    FormField {
        label: qsTr("Name")
        InputField {
            text: root.name
            placeholderText: qsTr("Rack")
            onTextEdited: root.name = text
        }
    }

    FormField {
        label: qsTr("Devices")
        help: Catalog.devices.length === 0 ? qsTr("Add a device first.") : ""
        Column {
            spacing: 2
            Repeater {
                model: Catalog.devices
                delegate: CheckRow {
                    required property var modelData
                    text: modelData.name
                    checked: root.members.indexOf(modelData.id) >= 0
                    onToggled: root.setMember(modelData.id, checked)
                }
            }
        }
    }

    Text {
        Layout.fillWidth: true
        visible: root.error.length > 0
        text: root.error
        color: Theme.critical
        wrapMode: Text.Wrap
    }

    footerItems: [
        PillButton {
            iconName: "check"
            text: root.groupId.length > 0 ? qsTr("Save") : qsTr("Add group")
            onClicked: {
                const id = Catalog.saveGroup(root.groupId, root.name, root.members)
                if (id.length > 0)
                    root.close()
                else
                    root.error = Catalog.lastError
            }
        },
        PillButton {
            kind: "secondary"
            text: qsTr("Cancel")
            onClicked: root.close()
        }
    ]
}
