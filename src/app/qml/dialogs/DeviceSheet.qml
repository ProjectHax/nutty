// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Layouts
import Nutty

// Add or edit a device. A new device starts with picking its type.
Sheet {
    id: root

    required property DeviceEditor editor
    property bool choosingType: editor.isNew && !typeChosen
    property bool typeChosen: false
    property bool showAdvanced: false
    property string imageUrl

    signal saved(string id)

    function startNew() {
        typeChosen = false
        showAdvanced = false
        open()
    }

    // Moves on from the type picker to the form for `type`.
    function choose(type) {
        editor.reset(type)
        typeChosen = true
        // Already set up on this account: list what else is there.
        if (editor.reusedFrom.length > 0 && editor.discoverySupported)
            editor.discover()
    }

    title: choosingType ? qsTr("Add a device") : editor.isNew ? qsTr("New %1 device").arg(editor.typeName) : qsTr("Edit %1").arg(editor.name)
    iconName: choosingType ? "plus" : "devices"
    preferredWidth: 660

    onClosed: editor.cancelTest()

    ImagePicker {
        id: picker
        deviceId: root.editor.deviceId
        onPicked: fileName => root.editor.customImage = fileName
    }

    // ------------------------------------------------------- type picker
    TypePicker {
        visible: root.choosingType
        model: Catalog.sourceTypes
        onPicked: key => root.choose(key)
    }

    // -------------------------------------------------------------- form
    ColumnLayout {
        Layout.fillWidth: true
        visible: !root.choosingType
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            DeviceArt {
                Layout.preferredWidth: 84
                Layout.preferredHeight: 84
                kind: root.editor.kind === "Auto" ? (root.editor.type === "EcoFlow" ? "PowerStation" : root.editor.type === "System" ? "Laptop" : root.editor.type === "HttpJson" ? "HomeBattery" : "TowerUps") : root.editor.kind
                imageSource: root.editor.imageSource
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 10
                FormField {
                    label: qsTr("Name")
                    InputField {
                        placeholderText: qsTr("e.g. Office UPS")
                        text: root.editor.name
                        onTextEdited: root.editor.name = text
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    PillButton {
                        kind: "secondary"
                        iconName: "image"
                        text: qsTr("Choose picture…")
                        onClicked: picker.chooseFile()
                    }
                    PillButton {
                        kind: "ghost"
                        visible: root.editor.customImage.length > 0
                        text: qsTr("Use artwork")
                        onClicked: root.editor.customImage = ""
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            InputField {
                id: urlField
                Layout.fillWidth: true
                placeholderText: qsTr("Or paste a picture's web address")
                inputMethodHints: Qt.ImhUrlCharactersOnly
            }
            PillButton {
                kind: "secondary"
                iconName: "download"
                text: picker.busy ? qsTr("Downloading…") : qsTr("Download")
                enabled: urlField.text.length > 0 && !picker.busy
                onClicked: picker.download(urlField.text)
            }
        }
        Text {
            Layout.fillWidth: true
            visible: picker.error.length > 0
            text: picker.error
            color: Theme.critical
            font.pixelSize: Theme.fontSizeSmall
            wrapMode: Text.Wrap
        }

        FormField {
            label: qsTr("Looks like")
            Dropdown {
                options: Catalog.deviceKinds.map(k => ({ value: k.key, text: k.name }))
                value: root.editor.kind
                onPicked: v => root.editor.kind = v
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        // Account details taken from, or shared with, other devices.
        Rectangle {
            Layout.fillWidth: true
            visible: root.editor.accountNote.length > 0
            implicitHeight: accountColumn.implicitHeight + 20
            radius: Theme.radius
            color: Theme.alpha(Theme.info, 0.1)
            ColumnLayout {
                id: accountColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 10
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Icon {
                        name: "link"
                        size: 16
                        color: Theme.info
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.editor.accountNote
                        color: Theme.text
                        wrapMode: Text.Wrap
                    }
                    PillButton {
                        visible: root.editor.isNew
                        kind: "ghost"
                        text: qsTr("Use different ones")
                        onClicked: root.editor.startBlank()
                    }
                }
                CheckRow {
                    Layout.fillWidth: true
                    visible: !root.editor.isNew && root.editor.accountEdited
                    text: qsTr("Make the same change to %n other device(s)", "", root.editor.sharedWith.length)
                    checked: root.editor.applyToShared
                    onToggled: root.editor.applyToShared = checked
                }
            }
        }

        Repeater {
            model: root.editor.fields.filter(f => !f.advanced)
            delegate: SpecField {
                required property var modelData
                editor: root.editor
                spec: modelData
                onDiscoverRequested: root.editor.discover()
            }
        }

        // Discovery results.
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.editor.discovering || root.editor.discovered.length > 0 || root.editor.discoveryError.length > 0
            spacing: 6
            Text {
                Layout.fillWidth: true
                text: root.editor.discovering ? qsTr("Looking…") : root.editor.discoveryError.length > 0 ? root.editor.discoveryError : qsTr("Found:")
                color: root.editor.discoveryError.length > 0 ? Theme.warning : Theme.subtext
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.Wrap
            }
            Repeater {
                model: root.editor.discovered
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    implicitHeight: Math.max(44, pickRow.implicitHeight + 16)
                    radius: Theme.radius
                    color: tap.pressed ? Theme.pressed : hoverPick.hovered ? Theme.hover : Theme.alpha(Theme.text, 0.035)
                    opacity: modelData.added ? 0.6 : 1
                    RowLayout {
                        id: pickRow
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 10
                        Icon {
                            name: "battery"
                            size: 16
                            color: modelData.online ? Theme.success : Theme.faint
                        }
                        // Names and descriptions come from the device and can be long.
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text {
                                Layout.fillWidth: true
                                text: modelData.name
                                color: Theme.text
                                font.weight: Font.DemiBold
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: text.length > 0
                                text: modelData.description
                                color: Theme.subtext
                                font.pixelSize: Theme.fontSizeSmall
                                elide: Text.ElideRight
                            }
                        }
                        Text {
                            objectName: "useDiscovered"
                            text: modelData.added ? qsTr("Added", "already set up as a device") : qsTr("Use")
                            color: modelData.added ? Theme.subtext : Theme.accent
                            font.weight: Font.DemiBold
                        }
                    }
                    HoverHandler { id: hoverPick; enabled: !modelData.added; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        id: tap
                        enabled: !modelData.added
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: root.editor.pick(index)
                    }
                }
            }
        }

        // Advanced.
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Icon {
                name: root.showAdvanced ? "chevron-down" : "chevron-right"
                size: 14
                color: Theme.subtext
            }
            Text {
                text: qsTr("Advanced")
                color: Theme.subtext
                font.weight: Font.DemiBold
            }
            Item { Layout.fillWidth: true }
            TapHandler {
                gesturePolicy: TapHandler.ReleaseWithinBounds
                onTapped: root.showAdvanced = !root.showAdvanced
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: root.showAdvanced
            spacing: 14

            Repeater {
                model: root.editor.fields.filter(f => f.advanced)
                delegate: SpecField {
                    required property var modelData
                    editor: root.editor
                    spec: modelData
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 12
                rowSpacing: 14
                FormField {
                    label: qsTr("Check every")
                    help: qsTr("0 uses the default (%1 s).").arg(root.editor.defaultPollIntervalSec)
                    InputField {
                        suffix: qsTr("s")
                        validator: IntValidator { bottom: 0; top: 3600 }
                        text: String(root.editor.pollIntervalSec)
                        onTextEdited: root.editor.pollIntervalSec = parseInt(text || "0")
                    }
                }
                FormField {
                    label: qsTr("Notify below charge")
                    help: qsTr("0 turns it off.")
                    InputField {
                        suffix: "%"
                        validator: IntValidator { bottom: 0; top: 100 }
                        text: String(root.editor.notifyChargeBelow)
                        onTextEdited: root.editor.notifyChargeBelow = parseInt(text || "0")
                    }
                }
                FormField {
                    label: qsTr("Notify below runtime")
                    help: qsTr("In minutes; 0 turns it off.")
                    InputField {
                        suffix: qsTr("min")
                        validator: IntValidator { bottom: 0; top: 1440 }
                        text: String(Math.round(root.editor.notifyRuntimeBelowSec / 60))
                        onTextEdited: root.editor.notifyRuntimeBelowSec = parseInt(text || "0") * 60
                    }
                }
            }
            Toggle {
                Layout.fillWidth: true
                text: qsTr("Losing contact while on battery is critical")
                checked: root.editor.commLostCritical
                onToggled: root.editor.commLostCritical = checked
            }
            Toggle {
                Layout.fillWidth: true
                text: qsTr("Monitor this device")
                checked: root.editor.enabled
                onToggled: root.editor.enabled = checked
            }
            Text {
                Layout.fillWidth: true
                visible: Catalog.channels.length > 0
                text: qsTr("Notify through")
                color: Theme.subtext
                font.pixelSize: Theme.fontSizeSmall
                font.weight: Font.DemiBold
            }
            Flow {
                Layout.fillWidth: true
                spacing: 12
                CheckRow {
                    text: qsTr("Every channel")
                    checked: root.editor.channels.length === 0
                    onToggled: if (checked) root.editor.channels = []
                }
                Repeater {
                    model: Catalog.channels
                    delegate: CheckRow {
                        required property var modelData
                        text: modelData.name
                        checked: root.editor.channels.indexOf(modelData.id) >= 0
                        onToggled: {
                            const list = root.editor.channels.slice()
                            const i = list.indexOf(modelData.id)
                            if (checked && i < 0)
                                list.push(modelData.id)
                            else if (!checked && i >= 0)
                                list.splice(i, 1)
                            root.editor.channels = list
                        }
                    }
                }
            }
        }

        // Test result.
        Rectangle {
            Layout.fillWidth: true
            visible: root.editor.testing || root.editor.testDone
            implicitHeight: testRow.implicitHeight + 20
            radius: Theme.radius
            color: Theme.alpha(root.editor.testing ? Theme.info : root.editor.testOk ? Theme.success : Theme.critical, 0.12)
            RowLayout {
                id: testRow
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10
                Icon {
                    name: root.editor.testing ? "refresh" : root.editor.testOk ? "check-circle" : "warning"
                    size: 16
                    color: root.editor.testing ? Theme.info : root.editor.testOk ? Theme.success : Theme.critical
                }
                Text {
                    Layout.fillWidth: true
                    text: root.editor.testing ? qsTr("Connecting…") : root.editor.testResult
                    color: Theme.text
                    wrapMode: Text.Wrap
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: root.editor.error.length > 0
            text: root.editor.error
            color: Theme.critical
            wrapMode: Text.Wrap
        }
    }

    leadingFooterItems: [
        PillButton {
            visible: !root.choosingType
            kind: "secondary"
            iconName: "test"
            text: qsTr("Test connection")
            enabled: !root.editor.testing
            onClicked: root.editor.testConnection()
        }
    ]

    footerItems: [
        PillButton {
            visible: !root.choosingType
            iconName: "check"
            text: root.editor.isNew ? qsTr("Add device") : qsTr("Save")
            onClicked: {
                if (root.editor.save()) {
                    root.saved(root.editor.deviceId)
                    root.close()
                }
            }
        },
        PillButton {
            kind: "secondary"
            text: qsTr("Cancel")
            onClicked: root.close()
        }
    ]
}
