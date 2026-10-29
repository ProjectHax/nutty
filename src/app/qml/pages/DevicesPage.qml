// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Layouts
import Nutty

// Every configured device: open, edit, pause, reorder or remove it, and add
// new ones.
ScrollPage {
    id: page

    // "add" opens the add-device sheet right away, "add:<type>" its form
    // for that type.
    property string request

    Component.onCompleted: handleRequest()
    onRequestChanged: handleRequest()

    function handleRequest() {
        if (request === "add" || request.startsWith("add:"))
            addDevice(request.substring(4))
    }

    function addDevice(type) {
        editor.reset("Nut")
        sheet.startNew()
        if (type)
            sheet.choose(type)
    }

    SectionHeader {
        text: qsTr("Devices")
        detail: Devices.count > 0 ? qsTr("%n configured", "", Devices.count) : ""
        PillButton {
            iconName: "plus"
            text: qsTr("Add device")
            onClicked: page.addDevice()
        }
    }

    Card {
        Layout.fillWidth: true
        visible: Devices.count === 0
        EmptyState {
            width: parent.width
            iconName: "devices"
            title: qsTr("No devices yet")
            actionText: qsTr("Add device")
            onActionClicked: page.addDevice()
        }
    }

    Card {
        Layout.fillWidth: true
        visible: Devices.count > 0
        padding: Theme.listPadding
        ColumnLayout {
            id: rows
            width: parent.width
            spacing: 0

            // Every status chip is as wide as the widest one, so the chips
            // and the charge next to them line up from row to row.
            property real chipWidth: 0
            function fitChips() {
                let widest = 0
                for (let i = 0; i < deviceRows.count; ++i) {
                    const item = deviceRows.itemAt(i)
                    if (item)
                        widest = Math.max(widest, item.chipWidth)
                }
                chipWidth = widest
            }

            TextMetrics {
                id: fullCharge
                font.pixelSize: Theme.fontSizeLarge
                font.weight: Font.DemiBold
                text: Format.percent(100)
            }

            Repeater {
                id: deviceRows
                model: Devices
                onItemAdded: Qt.callLater(rows.fitChips)
                onItemRemoved: Qt.callLater(rows.fitChips)
                delegate: Item {
                    id: row
                    Layout.fillWidth: true
                    implicitHeight: 76
                    readonly property string deviceId: model.deviceId
                    readonly property real chipWidth: chip.implicitWidth
                    onChipWidthChanged: Qt.callLater(rows.fitChips)

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radius
                        color: rowHover.hovered ? Theme.hover : "transparent"
                    }
                    Rectangle {
                        visible: model.index > 0
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: Theme.rowInset
                        anchors.rightMargin: Theme.rowInset
                        height: 1
                        color: Theme.divider
                    }
                    HoverHandler { id: rowHover }
                    TapHandler {
                        onTapped: page.navigate("device", row.deviceId)
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.rowInset
                        anchors.rightMargin: Theme.rowInset
                        spacing: 14

                        DeviceArt {
                            Layout.preferredWidth: 54
                            Layout.preferredHeight: 54
                            kind: model.kind
                            imageSource: model.imageSource
                            dimmed: model.offline
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Text {
                                Layout.fillWidth: true
                                text: model.name
                                color: model.enabled ? Theme.text : Theme.faint
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: model.subtitle
                                color: Theme.subtext
                                font.pixelSize: Theme.fontSizeSmall
                                elide: Text.ElideRight
                            }
                        }
                        // Room for "100 %" even when empty, for the same reason.
                        Text {
                            visible: page.contentWidthUsed > 620
                            Layout.preferredWidth: Math.ceil(fullCharge.advanceWidth)
                            horizontalAlignment: Text.AlignRight
                            text: model.charge >= 0 && !model.offline ? Format.percent(model.charge) : ""
                            color: Theme.text
                            font: fullCharge.font
                        }
                        StatusChip {
                            id: chip
                            Layout.preferredWidth: Math.max(implicitWidth, rows.chipWidth)
                            status: model.status
                            text: model.statusText
                        }
                        Toggle {
                            checked: model.enabled
                            onToggled: Catalog.setDeviceEnabled(row.deviceId, checked)
                            Accessible.name: qsTr("Monitor %1").arg(model.name)
                        }
                        IconButton {
                            iconName: "edit"
                            toolTip: qsTr("Edit")
                            onClicked: {
                                editor.load(row.deviceId)
                                sheet.open()
                            }
                        }
                        IconButton {
                            iconName: "more"
                            toolTip: qsTr("More")
                            onClicked: menu.popup()
                            DeviceMenu {
                                id: menu
                                first: model.index === 0
                                last: model.index === Devices.count - 1
                                onMoveUp: Catalog.moveDevice(row.deviceId, -1)
                                onMoveDown: Catalog.moveDevice(row.deviceId, 1)
                                onRemove: {
                                    confirmRemove.deviceId = row.deviceId
                                    confirmRemove.title = qsTr("Remove %1?").arg(model.name)
                                    confirmRemove.open()
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Text {
        Layout.fillWidth: true
        visible: Catalog.lastError.length > 0
        text: Catalog.lastError
        color: Theme.critical
        wrapMode: Text.Wrap
    }

    DeviceEditor {
        id: editor
    }

    DeviceSheet {
        id: sheet
        editor: editor
        onSaved: id => page.navigate("device", id)
    }

    ConfirmSheet {
        id: confirmRemove
        property string deviceId
        message: qsTr("Its rules and its history go with it. This can't be undone.")
        onAccepted: Catalog.removeDevice(deviceId)
    }
}
