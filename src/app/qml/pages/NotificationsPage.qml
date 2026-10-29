// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Layouts
import Nutty

// Where notifications go: desktop, Pushover, ntfy, email, webhooks and PagerDuty.
ScrollPage {
    id: page

    // "add" opens the add-channel sheet right away, "add:<type>" its form
    // for that type.
    property string request

    Component.onCompleted: handleRequest()
    onRequestChanged: handleRequest()

    function handleRequest() {
        if (request === "add" || request.startsWith("add:"))
            addChannel(request.substring(4))
    }

    function addChannel(type) {
        editor.reset("Desktop")
        sheet.startNew()
        if (type)
            sheet.choose(type)
    }

    SectionHeader {
        text: qsTr("Notification channels")
        detail: qsTr("%n channel(s)", "", Catalog.channels.length)
        PillButton {
            iconName: "plus"
            text: qsTr("Add channel")
            onClicked: page.addChannel()
        }
    }

    Card {
        Layout.fillWidth: true
        visible: Catalog.channels.length === 0
        EmptyState {
            width: parent.width
            iconName: "bell"
            title: qsTr("No channels")
            actionText: qsTr("Add channel")
            onActionClicked: page.addChannel()
        }
    }

    Card {
        Layout.fillWidth: true
        visible: Catalog.channels.length > 0
        padding: Theme.listPadding
        ColumnLayout {
            width: parent.width
            spacing: 0
            Repeater {
                model: Catalog.channels
                delegate: ListRow {
                    id: row
                    required property var modelData
                    required property int index
                    divider: index > 0
                    iconName: modelData.icon
                    iconColor: modelData.enabled ? Theme.subtext : Theme.faint
                    title: modelData.name
                    titleColor: modelData.enabled ? Theme.text : Theme.faint
                    description: [modelData.typeName, modelData.description,
                                  qsTr("%n event type(s)", "", modelData.eventCount)]
                                 .filter(part => part.length > 0).join(" · ")
                    elideTitle: true
                    elideDescription: true

                    PillButton {
                        kind: "ghost"
                        iconName: "send"
                        text: qsTr("Test")
                        visible: page.contentWidthUsed > 560
                        onClicked: {
                            tester.load(row.modelData.id)
                            tester.sendTest()
                        }
                    }
                    Toggle {
                        checked: row.modelData.enabled
                        onToggled: Catalog.setChannelEnabled(row.modelData.id, checked)
                    }
                    IconButton {
                        iconName: "edit"
                        toolTip: qsTr("Edit")
                        onClicked: {
                            editor.load(row.modelData.id)
                            sheet.open()
                        }
                    }
                    IconButton {
                        iconName: "trash"
                        danger: true
                        toolTip: qsTr("Remove")
                        onClicked: {
                            confirmRemove.channelId = row.modelData.id
                            confirmRemove.title = qsTr("Remove %1?").arg(row.modelData.name)
                            confirmRemove.open()
                        }
                    }
                }
            }
        }
    }

    // The result of a Test from the list.
    Rectangle {
        Layout.fillWidth: true
        visible: tester.testing || tester.testDone
        implicitHeight: testText.implicitHeight + 20
        radius: Theme.radius
        color: Theme.alpha(tester.testing ? Theme.info : tester.testOk ? Theme.success : Theme.critical, 0.12)
        Text {
            id: testText
            anchors.fill: parent
            anchors.margins: 10
            text: tester.testing ? qsTr("Sending a test to %1…").arg(tester.name)
                                 : qsTr("%1: %2").arg(tester.name).arg(tester.testResult)
            color: Theme.text
            wrapMode: Text.Wrap
        }
    }

    ChannelEditor { id: editor }
    ChannelEditor { id: tester }

    ChannelSheet {
        id: sheet
        editor: editor
    }

    ConfirmSheet {
        id: confirmRemove
        property string channelId
        message: qsTr("nutty stops sending notifications there.")
        onAccepted: Catalog.removeChannel(channelId)
    }
}
