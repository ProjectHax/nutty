// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Layouts
import Nutty

// Add or edit a power rule: what to watch, when, and what to do.
Sheet {
    id: root

    required property RuleEditor editor
    readonly property bool groupScope: editor.scope !== "Device"
    readonly property var capability: editor.capability

    function optionsOf(list) {
        return list.map(e => ({ value: e.key, text: e.name }))
    }

    title: editor.isNew ? qsTr("New power rule") : qsTr("Edit %1").arg(editor.name.length > 0 ? editor.name : editor.suggestedName)
    subtitle: editor.summary
    iconName: "power"
    preferredWidth: 600

    FormField {
        label: qsTr("Watch")
        Segmented {
            options: root.optionsOf(Catalog.scopes)
            value: root.editor.scope
            onActivated: value => root.editor.scope = value
        }
    }

    FormField {
        label: root.groupScope ? qsTr("Group") : qsTr("Device")
        help: root.editor.targets.length === 0
              ? (root.groupScope ? qsTr("Make a group below first.") : qsTr("Add a device first."))
              : ""
        Dropdown {
            options: root.editor.targets.map(t => ({ value: t.id, text: t.name }))
            value: root.editor.target
            onPicked: value => root.editor.target = value
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 12
        FormField {
            label: qsTr("When")
            Dropdown {
                options: root.optionsOf(Catalog.conditions)
                value: root.editor.condition
                onPicked: value => root.editor.condition = value
            }
        }
        FormField {
            Layout.preferredWidth: 150
            Layout.fillWidth: false
            visible: root.editor.unit.length > 0
            label: root.editor.unit === "%" ? qsTr("Charge") : qsTr("Seconds")
            InputField {
                text: String(Math.round(root.editor.threshold))
                suffix: root.editor.unit
                validator: IntValidator { bottom: 0; top: root.editor.unit === "%" ? 100 : 86400 }
                onEditingFinished: root.editor.threshold = Number(text)
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 12
        FormField {
            label: qsTr("Then")
            Dropdown {
                options: root.optionsOf(Catalog.actionTypes)
                value: root.editor.action
                onPicked: value => root.editor.action = value
            }
        }
        FormField {
            Layout.preferredWidth: 150
            Layout.fillWidth: false
            visible: root.editor.hasCountdown
            label: qsTr("Countdown")
            InputField {
                text: String(root.editor.countdownSec)
                suffix: "s"
                validator: IntValidator { bottom: 0; top: 3600 }
                onEditingFinished: root.editor.countdownSec = Number(text)
            }
        }
    }

    Text {
        Layout.fillWidth: true
        visible: root.editor.condition === "ForcedShutdown"
        text: qsTr("A forced shutdown comes from the UPS's NUT server. nutty acts after a few seconds, without a countdown, and it can't be cancelled.")
        color: Theme.subtext
        font.pixelSize: Theme.fontSizeSmall
        wrapMode: Text.Wrap
    }

    FormField {
        visible: root.editor.action === "Command"
        label: qsTr("Command")
        help: qsTr("Runs in the system shell.")
        InputField {
            text: root.editor.command
            placeholderText: qsTr("/path/to/script --flag")
            onTextEdited: root.editor.command = text
        }
    }

    Rectangle {
        Layout.fillWidth: true
        visible: root.capability.available === false || (root.capability.reason || "").length > 0
        implicitHeight: capRow.implicitHeight + 20
        radius: Theme.radius
        color: Theme.alpha(root.capability.available === false ? Theme.critical : Theme.warning, 0.12)
        RowLayout {
            id: capRow
            anchors.fill: parent
            anchors.margins: 10
            spacing: 10
            Icon {
                Layout.alignment: Qt.AlignTop
                name: "warning"
                size: 16
                color: root.capability.available === false ? Theme.critical : Theme.warning
            }
            Text {
                Layout.fillWidth: true
                text: root.capability.available === false
                      ? qsTr("This computer can't do that: %1").arg(root.capability.reason)
                      : root.capability.reason
                color: Theme.text
                wrapMode: Text.Wrap
            }
        }
    }

    FormField {
        label: qsTr("Name")
        InputField {
            text: root.editor.name
            placeholderText: root.editor.suggestedName
            onTextEdited: root.editor.name = text
        }
    }

    CheckRow {
        text: qsTr("Enabled")
        checked: root.editor.enabled
        onToggled: root.editor.enabled = checked
    }

    Text {
        Layout.fillWidth: true
        visible: root.editor.error.length > 0
        text: root.editor.error
        color: Theme.critical
        wrapMode: Text.Wrap
    }

    leadingFooterItems: [
        PillButton {
            kind: "secondary"
            iconName: "play"
            text: qsTr("Simulate")
            enabled: root.editor.targets.length > 0 && root.editor.action !== "Notify"
            toolTip: qsTr("Saves the rule and runs its countdown without doing anything")
            onClicked: if (root.editor.simulate()) root.close()
        }
    ]

    footerItems: [
        PillButton {
            iconName: "check"
            text: root.editor.isNew ? qsTr("Add rule") : qsTr("Save")
            enabled: root.editor.targets.length > 0
            onClicked: if (root.editor.save()) root.close()
        },
        PillButton {
            kind: "secondary"
            text: qsTr("Cancel")
            onClicked: root.close()
        }
    ]
}
