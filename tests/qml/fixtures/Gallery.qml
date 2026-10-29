// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Nutty
import "GalleryData.js" as Mock

// A test fixture showing every component with made-up data, inside the
// same frameless chrome the real main window uses.
Window {
    id: win

    property string page: "home"
    readonly property real now: Date.now()
    readonly property var devices: Mock.devices(now)
    readonly property var history: Mock.series(now)

    width: 1280
    height: 820
    minimumWidth: 720
    minimumHeight: 520
    visible: true
    title: "nutty — component gallery"
    flags: Theme.frameless ? (Qt.Window | Qt.FramelessWindowHint) : Qt.Window
    color: Theme.frameless ? "transparent" : Theme.window

    ListModel { id: activityModel }
    Component.onCompleted: {
        const rows = Mock.activity(now)
        for (let i = 0; i < rows.length; ++i)
            activityModel.append(rows[i])
    }

    WindowFrame {
        id: frame
        window: win

        RowLayout {
            anchors.fill: parent
            spacing: 0

            Sidebar {
                Layout.fillHeight: true
                window: win
                compact: win.width < 900
                cornerRadius: Math.max(0, frame.radius - 1)
                subtitle: qsTr("Power monitor")
                currentKey: win.page
                items: [
                    { key: "home", text: qsTr("Home"), icon: "home" },
                    { key: "components", text: qsTr("Components"), icon: "sliders" },
                    { key: "art", text: qsTr("Artwork"), icon: "image" },
                    { key: "history", text: qsTr("History"), icon: "history", badge: 2 },
                    { key: "notifications", text: qsTr("Notifications"), icon: "bell" },
                    { key: "actions", text: qsTr("Actions"), icon: "shield" },
                ]
                footerItems: [
                    { key: "settings", text: qsTr("Settings"), icon: "gear" },
                    { key: "about", text: qsTr("About"), icon: "info" },
                ]
                onActivated: key => win.page = key
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                TitleBar {
                    Layout.fillWidth: true
                    window: win
                    title: win.page === "components" ? qsTr("Components") : win.page === "art" ? qsTr("Artwork") : qsTr("Home")

                    IconButton {
                        iconName: "refresh"
                        toolTip: qsTr("Refresh")
                    }
                    IconButton {
                        iconName: Theme.dark ? "sun" : "moon"
                        toolTip: Theme.dark ? qsTr("Light mode") : qsTr("Dark mode")
                        onClicked: Theme.mode = Theme.dark ? "Light" : "Dark"
                    }
                }

                Loader {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    sourceComponent: win.page === "components" ? componentsPage : win.page === "art" ? artPage : homePage
                }
            }
        }
    }

    component Page: Flickable {
        id: flick
        default property alias body: column.data
        clip: true
        contentWidth: width
        contentHeight: column.implicitHeight + 40
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        ColumnLayout {
            id: column
            x: 24
            y: 8
            width: flick.width - 48
            spacing: 18
        }
    }

    component SectionTitle: RowLayout {
        property alias text: label.text
        property alias detail: detailLabel.text
        Layout.fillWidth: true
        Layout.topMargin: 6
        spacing: 10
        Text {
            id: label
            color: Theme.text
            font.pixelSize: Theme.fontSizeLarge
            font.weight: Font.Bold
        }
        Text {
            id: detailLabel
            color: Theme.faint
            Layout.fillWidth: true
        }
    }

    // ------------------------------------------------------------------ home
    Component {
        id: homePage
        Page {
            id: home
            readonly property int columns: Math.max(1, Math.floor((width - 48 + 16) / (330 + 16)))

            SummaryBanner {
                Layout.fillWidth: true
                level: "warning"
                title: qsTr("Rack UPS is on battery")
                subtitle: qsTr("63 % · about 21 min left. Shutdown starts at 20 %.")
                actionText: qsTr("Details")
            }

            SectionTitle {
                text: qsTr("Devices")
                detail: qsTr("%1 monitored").arg(win.devices.length)
                PillButton {
                    kind: "secondary"
                    iconName: "plus"
                    text: qsTr("Add device")
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: home.columns
                columnSpacing: 16
                rowSpacing: 16
                Repeater {
                    model: win.devices
                    DeviceCard {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredWidth: 330
                        Layout.alignment: Qt.AlignTop
                        name: modelData.name
                        subtitle: modelData.subtitle
                        kind: modelData.kind
                        status: modelData.status
                        charge: modelData.charge
                        runtime: modelData.runtime
                        load: modelData.load
                        wattsIn: modelData.wattsIn
                        wattsOut: modelData.wattsOut
                        lastUpdated: modelData.updated
                        history: modelData.history
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: home.width > 900 ? 2 : 1
                columnSpacing: 16
                rowSpacing: 16

                Card {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 3
                    Layout.alignment: Qt.AlignTop
                    implicitHeight: 330
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 10
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: qsTr("Rack UPS · charge")
                                color: Theme.text
                                font.pixelSize: Theme.fontSizeLarge
                                font.weight: Font.DemiBold
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: qsTr("Last 24 hours")
                                color: Theme.faint
                            }
                        }
                        RangeChart {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            points: win.history.points
                            bands: win.history.bands
                            xFrom: win.now - 24 * 3600 * 1000
                            xTo: win.now
                            color: Theme.accent
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
                            }
                        }
                        ActivityList {
                            Layout.fillWidth: true
                            interactive: false
                            model: activityModel
                        }
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------ components
    Component {
        id: componentsPage
        Page {
            SectionTitle { text: qsTr("Battery") }
            Flow {
                Layout.fillWidth: true
                spacing: 16
                Repeater {
                    model: [
                        { level: 100, charging: true, offline: false, stale: false },
                        { level: 63, charging: false, offline: false, stale: false },
                        { level: 12, charging: false, offline: false, stale: false },
                        { level: 48, charging: false, offline: false, stale: true },
                        { level: -1, charging: false, offline: true, stale: false },
                    ]
                    Card {
                        required property var modelData
                        width: 170
                        height: 120
                        BatteryVisual {
                            anchors.centerIn: parent
                            width: 128
                            height: 60
                            level: modelData.level
                            charging: modelData.charging
                            offline: modelData.offline
                            stale: modelData.stale
                        }
                    }
                }
                Card {
                    width: 200
                    height: 120
                    Row {
                        anchors.centerIn: parent
                        spacing: 22
                        BatteryVisual { width: 36; height: 72; vertical: true; level: 88; charging: true; showLabel: false }
                        BatteryVisual { width: 36; height: 72; vertical: true; level: 40; showLabel: false }
                        BatteryVisual { width: 36; height: 72; vertical: true; level: 8; showLabel: false }
                    }
                }
            }

            SectionTitle { text: qsTr("Gauges and charts") }
            Flow {
                Layout.fillWidth: true
                spacing: 16
                Card {
                    width: 470
                    height: 200
                    Row {
                        anchors.centerIn: parent
                        spacing: 24
                        ArcGauge { width: 130; height: 130; value: 63; caption: qsTr("Charge") }
                        ArcGauge { width: 130; height: 130; value: 41; caption: qsTr("Load"); colorEnd: Theme.info }
                        ArcGauge {
                            width: 130; height: 130; value: 21; maximum: 60
                            valueText: "21"; unit: qsTr("min"); caption: qsTr("Runtime")
                            colorEnd: Theme.warning
                        }
                    }
                }
                Card {
                    width: 360
                    height: 200
                    Column {
                        anchors.fill: parent
                        spacing: 14
                        Sparkline { width: parent.width; height: 44; values: Mock.wave(60, 40, 95, 1.5) }
                        Sparkline { width: parent.width; height: 44; values: Mock.drain(60, 100, 18); color: Theme.critical }
                        Sparkline { width: parent.width; height: 44; values: Mock.wave(60, 20, 60, 3); color: Theme.success; fill: false }
                    }
                }
            }

            SectionTitle { text: qsTr("States and buttons") }
            Card {
                Layout.fillWidth: true
                ColumnLayout {
                    width: parent.width
                    spacing: 16
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        Repeater {
                            model: ["online", "charging", "onBattery", "lowBattery", "fsd", "bypass", "overload",
                                    "replaceBattery", "offline", "stale", "error", "unknown"]
                            StatusChip {
                                required property string modelData
                                status: modelData
                            }
                        }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 10
                        PillButton { text: qsTr("Primary"); iconName: "check" }
                        PillButton { text: qsTr("Secondary"); kind: "secondary"; iconName: "plus" }
                        PillButton { text: qsTr("Ghost"); kind: "ghost" }
                        PillButton { text: qsTr("Shut down"); tint: Theme.critical; iconName: "power" }
                        PillButton { text: qsTr("Large"); large: true }
                        IconButton { iconName: "edit"; toolTip: qsTr("Edit") }
                        IconButton { iconName: "trash"; toolTip: qsTr("Delete"); danger: true }
                        IconButton { iconName: "gear"; round: true; toolTip: qsTr("Settings") }
                        IconButton { iconName: "search"; enabled: false }
                    }
                }
            }

            SectionTitle { text: qsTr("Banners and countdown") }
            GridLayout {
                Layout.fillWidth: true
                columns: width > 980 ? 2 : 1
                columnSpacing: 16
                rowSpacing: 16
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 16
                    SummaryBanner {
                        Layout.fillWidth: true
                        level: "ok"
                        title: qsTr("Everything is online")
                        subtitle: qsTr("4 devices on mains power, all charged.")
                    }
                    SummaryBanner {
                        Layout.fillWidth: true
                        level: "critical"
                        title: qsTr("Shutting down in 42 s")
                        subtitle: qsTr("Camping station is at 12 %.")
                        actionText: qsTr("Cancel")
                    }
                    SummaryBanner {
                        Layout.fillWidth: true
                        level: "info"
                        title: qsTr("Garage battery is offline")
                        subtitle: qsTr("Last seen 3 h ago.")
                    }
                }
                Card {
                    Layout.preferredWidth: 400
                    Layout.alignment: Qt.AlignTop | Qt.AlignHCenter
                    color: Theme.elevated
                    CountdownPanel {
                        width: 368
                        deviceName: "Camping station"
                        reason: qsTr("Battery below 15 %")
                        remaining: 42
                        total: 60
                    }
                }
            }

            SectionTitle { text: qsTr("Activity") }
            Card {
                Layout.fillWidth: true
                ActivityList {
                    width: parent.width
                    interactive: false
                    model: activityModel
                }
            }
        }
    }

    // ------------------------------------------------------------------- art
    Component {
        id: artPage
        Page {
            SectionTitle { text: qsTr("Device artwork") }
            Flow {
                Layout.fillWidth: true
                spacing: 16
                Repeater {
                    model: Mock.kinds
                    Card {
                        required property string modelData
                        required property int index
                        width: 200
                        height: 230
                        hoverable: true
                        Column {
                            anchors.centerIn: parent
                            spacing: 8
                            DeviceArt {
                                width: 160
                                height: 160
                                kind: modelData
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: modelData
                                color: Theme.subtext
                            }
                        }
                    }
                }
            }

            SectionTitle { text: qsTr("Icons"); detail: Mock.icons.length }
            Card {
                Layout.fillWidth: true
                Flow {
                    width: parent.width
                    spacing: 6
                    Repeater {
                        model: Mock.icons
                        Rectangle {
                            required property string modelData
                            width: 92
                            height: 64
                            radius: Theme.radius
                            color: Theme.alpha(Theme.text, 0.035)
                            Column {
                                anchors.centerIn: parent
                                spacing: 6
                                Icon {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    name: modelData
                                    size: 22
                                }
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: modelData
                                    color: Theme.faint
                                    font.pixelSize: 10
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
