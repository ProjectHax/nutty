// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Layouts
import Nutty

// Everything at a glance: how things are, every device, the last day's
// charge and what happened recently.
ScrollPage {
    id: page

    readonly property int columns: Math.max(1, Math.floor((contentWidthUsed + 16) / (330 + 16)))
    property string chartDevice: Catalog.devices.length > 0 ? Catalog.devices[0].id : ""
    readonly property real now: Series.revision, Series.now()
    readonly property var chart: Series.revision, Series.get(chartDevice, "charge", now - 24 * 3600 * 1000, now, 288)

    SummaryBanner {
        Layout.fillWidth: true
        level: Summary.level
        title: Summary.title
        subtitle: Summary.subtitle
        iconName: Summary.iconName
        actionText: Summary.actionText
        onActionClicked: {
            const key = Summary.actionKey
            if (key === "cancel")
                Actions.cancel()
            else if (key === "addDevice")
                page.navigate("devices", "add")
            else if (key.indexOf("device:") === 0)
                page.navigate("device", key.substring(7))
        }
    }

    SectionHeader {
        text: qsTr("Devices")
        detail: Devices.count > 0 ? qsTr("%n monitored", "", Devices.enabledCount) : ""
        PillButton {
            kind: "secondary"
            iconName: "plus"
            text: qsTr("Add device")
            onClicked: page.navigate("devices", "add")
        }
    }

    Card {
        Layout.fillWidth: true
        visible: Devices.count === 0
        EmptyState {
            width: parent.width
            iconName: "devices"
            title: qsTr("Nothing to watch yet")
            actionText: qsTr("Add device")
            onActionClicked: page.navigate("devices", "add")
        }
    }

    GridLayout {
        Layout.fillWidth: true
        visible: Devices.count > 0
        columns: page.columns
        columnSpacing: 16
        rowSpacing: 16
        Repeater {
            model: Devices
            DeviceCard {
                readonly property string deviceId: model.deviceId
                Layout.fillWidth: true
                Layout.preferredWidth: 330
                Layout.fillHeight: true
                name: model.name
                subtitle: model.subtitle
                kind: model.kind
                imageSource: model.imageSource
                status: model.status
                statusText: model.statusText
                charge: model.charge
                runtime: model.runtime
                load: model.load
                wattsIn: model.wattsIn
                wattsOut: model.wattsOut
                lastUpdated: model.lastUpdated
                history: model.history
                offline: model.offline
                opacity: model.enabled ? 1 : 0.6
                onClicked: page.navigate("device", deviceId)
            }
        }
    }

    GridLayout {
        Layout.fillWidth: true
        visible: Devices.count > 0
        columns: page.contentWidthUsed > 900 ? 2 : 1
        columnSpacing: 16
        rowSpacing: 16

        Card {
            Layout.fillWidth: true
            Layout.preferredWidth: 3
            Layout.alignment: Qt.AlignTop
            implicitHeight: 320
            ColumnLayout {
                anchors.fill: parent
                spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Charge · last 24 hours")
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeLarge
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Dropdown {
                        Layout.preferredWidth: 190
                        visible: Devices.count > 1
                        options: {
                            const list = []
                            for (const d of Catalog.devices)
                                list.push({ value: d.id, text: d.name })
                            return list
                        }
                        value: page.chartDevice
                        onPicked: v => page.chartDevice = v
                    }
                }
                RangeChart {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    points: page.chart.points
                    bands: page.chart.bands
                    xFrom: page.now - 24 * 3600 * 1000
                    xTo: page.now
                    color: Theme.accent
                    Text {
                        anchors.centerIn: parent
                        visible: parent.points.length === 0
                        text: qsTr("No readings in the last day yet.")
                        color: Theme.faint
                    }
                }
            }
        }

        Card {
            Layout.fillWidth: true
            Layout.preferredWidth: 2
            Layout.alignment: Qt.AlignTop
            ColumnLayout {
                width: parent.width
                spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: qsTr("Recent activity")
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeLarge
                        font.weight: Font.DemiBold
                    }
                    Item { Layout.fillWidth: true }
                    PillButton {
                        kind: "ghost"
                        text: qsTr("View all")
                        onClicked: page.navigate("history", "")
                    }
                }
                ActivityList {
                    Layout.fillWidth: true
                    interactive: false
                    model: EventLogModel { limit: 7 }
                }
            }
        }
    }
}
