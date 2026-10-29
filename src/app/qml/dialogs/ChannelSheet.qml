// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Layouts
import Nutty

// Add or edit a notification channel.
Sheet {
    id: root

    required property ChannelEditor editor
    property bool typeChosen: false
    readonly property bool choosingType: editor.isNew && !typeChosen
    property bool showAdvanced: false
    readonly property var groups: {
        const out = []
        const seen = {}
        for (const e of Catalog.eventTypes) {
            if (seen[e.group] === undefined) {
                seen[e.group] = out.length
                out.push({ name: e.group, events: [] })
            }
            out[seen[e.group]].events.push(e)
        }
        return out
    }

    function startNew() {
        typeChosen = false
        showAdvanced = false
        open()
    }

    // Moves on from the type picker to the form for `type`.
    function choose(type) {
        editor.reset(type)
        typeChosen = true
    }

    title: choosingType ? qsTr("Add a notification channel") : editor.isNew ? qsTr("New %1 channel").arg(editor.typeName) : qsTr("Edit %1").arg(editor.name)
    iconName: "bell"
    preferredWidth: 660

    TypePicker {
        visible: root.choosingType
        model: Catalog.channelTypes
        onPicked: key => root.choose(key)
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: !root.choosingType
        spacing: 14

        FormField {
            label: qsTr("Name")
            InputField {
                text: root.editor.name
                onTextEdited: root.editor.name = text
            }
        }

        Repeater {
            model: root.editor.fields.filter(f => !f.advanced)
            delegate: SpecField {
                required property var modelData
                editor: root.editor
                spec: modelData
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.editor.fields.some(f => f.advanced)
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
        Repeater {
            model: root.showAdvanced ? root.editor.fields.filter(f => f.advanced) : []
            delegate: SpecField {
                required property var modelData
                editor: root.editor
                spec: modelData
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        Text {
            text: qsTr("Send these events")
            color: Theme.text
            font.weight: Font.DemiBold
        }
        Repeater {
            model: root.groups
            delegate: ColumnLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 6
                Text {
                    text: modelData.name
                    color: Theme.subtext
                    font.pixelSize: Theme.fontSizeSmall
                    font.weight: Font.DemiBold
                }
                // In columns, so the boxes line up from group to group.
                GridLayout {
                    Layout.fillWidth: true
                    columns: width > 560 ? 3 : 2
                    columnSpacing: 16
                    rowSpacing: 4
                    Repeater {
                        model: modelData.events
                        delegate: CheckRow {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            text: modelData.name
                            checked: root.editor.events.indexOf(modelData.key) >= 0
                            onToggled: root.editor.setEvent(modelData.key, checked)
                        }
                    }
                }
            }
        }

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
                    name: root.editor.testing ? "send" : root.editor.testOk ? "check-circle" : "warning"
                    size: 16
                    color: root.editor.testing ? Theme.info : root.editor.testOk ? Theme.success : Theme.critical
                }
                Text {
                    Layout.fillWidth: true
                    text: root.editor.testing ? qsTr("Sending…") : root.editor.testResult
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
            iconName: "send"
            text: qsTr("Send test")
            enabled: !root.editor.testing
            onClicked: root.editor.sendTest()
        }
    ]

    footerItems: [
        PillButton {
            visible: !root.choosingType
            iconName: "check"
            text: root.editor.isNew ? qsTr("Add channel") : qsTr("Save")
            onClicked: if (root.editor.save()) root.close()
        },
        PillButton {
            kind: "secondary"
            text: qsTr("Cancel")
            onClicked: root.close()
        }
    ]
}
