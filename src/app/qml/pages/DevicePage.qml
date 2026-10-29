// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Layouts
import Nutty

// One device in detail: big visuals, where its power goes, its batteries,
// charts and log, the rules about it, the settings on the device that nutty
// can change (each one asked about first), and every raw value its source
// reports.
ScrollPage {
    id: page

    property string deviceId
    property var info: Devices.get(deviceId)
    property var raw: Devices.rawValues(deviceId)
    property string range: "24h"
    property string metric: "charge"
    readonly property bool known: info.deviceId !== undefined
    readonly property real spanMs: ({ "1h": 3600, "24h": 86400, "7d": 7 * 86400, "30d": 30 * 86400 })[range] * 1000
    readonly property real now: Series.revision, Series.now()
    readonly property var chart: Series.revision, Series.get(deviceId, metric, now - spanMs, now, 300)
    // The power chart shows what comes in beside what goes out.
    readonly property var chartIn: metric === "wattsOut"
                                   ? (Series.revision, Series.get(deviceId, "wattsIn", now - spanMs, now, 300))
                                   : ({ points: [], max: 0 })
    readonly property var metrics: {
        const list = [
            { value: "charge", text: qsTr("Charge") },
            { value: "runtime", text: qsTr("Runtime") },
        ]
        if (info.load >= 0)
            list.push({ value: "load", text: qsTr("Load") })
        list.push({ value: "wattsOut", text: qsTr("Power") })
        if (info.wattsSolar >= 0)
            list.push({ value: "wattsSolar", text: qsTr("Solar") })
        if (info.temperature !== undefined)
            list.push({ value: "temperature", text: qsTr("Temperature") })
        return list
    }
    property string energyRange: "7d"
    readonly property var energy: Series.revision, Series.energy(deviceId, energyRange === "30d" ? 30 : 7)
    readonly property var details: deviceSettings.details
    readonly property bool hasFlow: (details.inputs || []).length + (details.outputs || []).length > 0
    readonly property bool hasPacks: (details.packs || []).length > 0
    readonly property bool hasEnergy: energy.whIn > 0 || energy.whOut > 0 || (details.energy || []).length > 0
    readonly property var rules: Catalog.rules.filter(r => r.scope === "Device" && r.target === page.deviceId)

    function refresh() {
        info = Devices.get(deviceId)
        raw = Devices.rawValues(deviceId)
    }

    // The top of a chart with `ticks` steps: a round step, so the labels
    // read 0, 250, 500, ... rather than 0, 183, 365, ...
    function chartTop(peak, ticks) {
        const raw = Math.max(peak * 1.1, 10) / ticks
        const p = Math.pow(10, Math.floor(Math.log(raw) / Math.LN10))
        for (const f of [1, 2, 2.5, 5, 10]) {
            if (f * p >= raw)
                return f * p * ticks
        }
        return 10 * p * ticks
    }

    function scrollTo(item) {
        const y = item.mapToItem(page.contentItem, 0, 0).y - 12
        scroller.to = Math.max(0, Math.min(y, page.contentHeight - page.height))
        scroller.restart()
    }

    NumberAnimation {
        id: scroller
        target: page
        property: "contentY"
        duration: Theme.animationsEnabled ? Theme.durationSlow : 0
        easing.type: Easing.OutCubic
    }

    // A change to a setting on the device: asked about, then sent.
    property var askedRow: null
    property string askedId
    property real askedValue: 0
    function ask(row, id, value) {
        askedRow = row
        askedId = id
        askedValue = value
        confirmSetting.ask(deviceSettings.confirmation(id, value))
    }
    onDeviceIdChanged: refresh()

    Connections {
        target: Devices
        function onDataChanged() { page.refresh() }
        function onModelReset() { page.refresh() }
    }

    EmptyState {
        visible: !page.known
        iconName: "devices"
        title: qsTr("This device is gone")
        text: qsTr("It may have been removed.")
        actionText: qsTr("All devices")
        actionIcon: "chevron-left"
        onActionClicked: page.navigate("devices", "")
    }

    // ------------------------------------------------------------- overview
    Card {
        Layout.fillWidth: true
        visible: page.known
        padding: 22
        GridLayout {
            width: parent.width
            columns: width > 760 ? 3 : 1
            columnSpacing: 26
            rowSpacing: 18

            DeviceArt {
                Layout.preferredWidth: 168
                Layout.preferredHeight: 168
                Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
                kind: page.info.kind || "Generic"
                imageSource: page.info.imageSource || ""
                dimmed: page.info.offline === true
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    text: page.info.name || ""
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeTitle + 4
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: page.info.subtitle || ""
                    color: Theme.subtext
                    elide: Text.ElideRight
                }
                RowLayout {
                    spacing: 8
                    StatusChip {
                        status: page.info.status || "unknown"
                        text: page.info.statusText || ""
                    }
                    Text {
                        visible: page.info.onBatterySince !== undefined
                        text: page.info.onBatterySince !== undefined
                              ? qsTr("on battery since %1").arg(Format.time(page.info.onBatterySince)) : ""
                        color: Theme.warning
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: (page.info.error || "").length > 0 && page.info.offline === true
                    text: page.info.error || ""
                    color: Theme.critical
                    font.pixelSize: Theme.fontSizeSmall
                    wrapMode: Text.Wrap
                }
                Text {
                    Layout.fillWidth: true
                    text: page.info.lastUpdated !== undefined
                          ? qsTr("Updated %1").arg(Format.relativeTime(page.info.lastUpdated))
                          : qsTr("Waiting for data")
                    color: Theme.faint
                    font.pixelSize: Theme.fontSizeSmall
                }
                Flow {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    spacing: 8
                    PillButton {
                        kind: "secondary"
                        iconName: "edit"
                        text: qsTr("Edit")
                        onClicked: {
                            editor.load(page.deviceId)
                            deviceSheet.open()
                        }
                    }
                    PillButton {
                        visible: deviceSettings.available
                        kind: "secondary"
                        iconName: "sliders"
                        text: qsTr("Settings")
                        onClicked: page.scrollTo(settingsSection)
                    }
                    PillButton {
                        kind: "secondary"
                        iconName: page.info.enabled ? "pause" : "play"
                        text: page.info.enabled ? qsTr("Pause") : qsTr("Resume")
                        onClicked: Catalog.setDeviceEnabled(page.deviceId, !page.info.enabled)
                    }
                    PillButton {
                        kind: "ghost"
                        tint: Theme.critical
                        iconName: "trash"
                        text: qsTr("Remove")
                        onClicked: confirmRemove.open()
                    }
                }
            }

            Row {
                Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
                spacing: 14
                ArcGauge {
                    width: 118
                    height: 118
                    value: page.info.offline ? -1 : (page.info.charge !== undefined ? page.info.charge : -1)
                    caption: qsTr("Charge")
                }
                ArcGauge {
                    width: 118
                    height: 118
                    readonly property real minutes: page.info.runtime >= 0 ? page.info.runtime / 60 : -1
                    value: page.info.offline ? -1 : minutes
                    maximum: Math.max(60, Math.ceil(minutes / 30) * 30)
                    valueText: minutes >= 0 ? (minutes >= 100 ? Format.durationShort(page.info.runtime) : String(Math.round(minutes))) : ""
                    unit: minutes >= 0 && minutes < 100 ? qsTr("min") : ""
                    caption: qsTr("Runtime")
                    colorEnd: Theme.info
                }
                ArcGauge {
                    width: 118
                    height: 118
                    visible: page.info.load >= 0
                    value: page.info.offline || page.info.load === undefined ? -1 : page.info.load
                    caption: qsTr("Load")
                    colorEnd: page.info.load > 80 ? Theme.critical : page.info.load > 60 ? Theme.warning : Theme.success
                }
            }
        }
    }

    // ---------------------------------------------------------------- tiles
    GridLayout {
        Layout.fillWidth: true
        visible: page.known
        columns: Math.max(2, Math.min(6, Math.floor(page.contentWidthUsed / 170)))
        columnSpacing: 12
        rowSpacing: 12
        Repeater {
            model: [
                { icon: "plug", label: qsTr("Input"), value: page.info.inputVoltage >= 0 ? Format.volts(page.info.inputVoltage) : "—" },
                { icon: "bolt", label: qsTr("Output"), value: page.info.outputVoltage >= 0 ? Format.volts(page.info.outputVoltage) : "—" },
                { icon: "gauge", label: qsTr("Power out"), value: page.info.wattsOut >= 0 ? Format.watts(page.info.wattsOut) : "—" },
                { icon: "battery-charging", label: qsTr("Power in"), value: page.info.wattsIn >= 0 ? Format.watts(page.info.wattsIn) : "—" },
                { icon: "thermometer", label: qsTr("Temperature"), value: page.info.temperature !== undefined ? qsTr("%1 °C").arg(Number(page.info.temperature).toLocaleString(Qt.locale(), "f", 1)) : "—" },
                { icon: "info", label: qsTr("Serial number"), value: (page.info.serial || "").length > 0 ? page.info.serial : "—" },
            ].filter(t => t.value !== "—")
            delegate: Card {
                required property var modelData
                // Equal columns, whatever the values' lengths.
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                padding: 14
                ColumnLayout {
                    width: parent.width
                    spacing: 4
                    Row {
                        spacing: 6
                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: modelData.icon
                            size: 13
                            color: Theme.faint
                        }
                        Text {
                            text: modelData.label
                            color: Theme.faint
                            font.pixelSize: Theme.fontSizeSmall
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: modelData.value
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeLarge
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------ power now
    Card {
        Layout.fillWidth: true
        visible: page.known && page.hasFlow
        ColumnLayout {
            width: parent.width
            spacing: 16
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Power now")
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeLarge
                    font.weight: Font.DemiBold
                }
                Text {
                    visible: page.details.chargeMinutes !== undefined
                    text: page.details.chargeMinutes !== undefined
                          ? qsTr("Full in %1").arg(Format.duration(page.details.chargeMinutes * 60)) : ""
                    color: Theme.success
                    font.pixelSize: Theme.fontSizeSmall
                    font.weight: Font.DemiBold
                }
            }
            PowerFlow {
                Layout.fillWidth: true
                inputs: page.details.inputs || []
                outputs: page.details.outputs || []
                dimmed: page.info.offline === true
            }
        }
    }

    // ------------------------------------------------ batteries and energy
    GridLayout {
        Layout.fillWidth: true
        visible: page.known && (page.hasPacks || page.hasEnergy)
        columns: page.contentWidthUsed > 900 && page.hasPacks && page.hasEnergy ? 2 : 1
        columnSpacing: 16
        rowSpacing: 16

        Card {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            visible: page.hasPacks
            ColumnLayout {
                width: parent.width
                spacing: 16
                Text {
                    text: (page.details.packs || []).length > 1 ? qsTr("Batteries") : qsTr("Battery")
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeLarge
                    font.weight: Font.DemiBold
                }
                Repeater {
                    model: page.details.packs || []
                    delegate: ColumnLayout {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        spacing: 16
                        Rectangle {
                            visible: index > 0
                            Layout.fillWidth: true
                            implicitHeight: 1
                            color: Theme.divider
                        }
                        PackStats {
                            Layout.fillWidth: true
                            pack: modelData
                        }
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    visible: (page.details.temperatures || []).length > 0
                    spacing: 18
                    Repeater {
                        model: page.details.temperatures || []
                        delegate: Row {
                            required property var modelData
                            spacing: 6
                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: "thermometer"
                                size: 13
                                color: Theme.faint
                            }
                            Text {
                                text: qsTr("%1 %2").arg(modelData.title).arg(Format.celsius(modelData.value))
                                color: Theme.subtext
                                font.pixelSize: Theme.fontSizeSmall
                            }
                        }
                    }
                }
            }
        }

        Card {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            visible: page.hasEnergy
            ColumnLayout {
                width: parent.width
                spacing: 12
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Energy")
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeLarge
                        font.weight: Font.DemiBold
                    }
                    Segmented {
                        compact: true
                        options: [
                            { value: "7d", text: qsTr("7 d") },
                            { value: "30d", text: qsTr("30 d") },
                        ]
                        value: page.energyRange
                        onActivated: v => page.energyRange = v
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 18
                    Repeater {
                        model: [
                            { color: Theme.success, text: qsTr("In %1").arg(Format.energy(page.energy.whIn)), shown: true },
                            { color: Theme.warning, text: qsTr("Solar %1").arg(Format.energy(page.energy.whSolar)), shown: page.energy.whSolar > 0 },
                            { color: Theme.accent, text: qsTr("Out %1").arg(Format.energy(page.energy.whOut)), shown: true },
                        ].filter(t => t.shown)
                        delegate: Row {
                            required property var modelData
                            spacing: 6
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 9
                                height: 9
                                radius: 2
                                color: modelData.color
                            }
                            Text {
                                text: modelData.text
                                color: Theme.text
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                }
                EnergyBars {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 170
                    days: page.energy.days
                    maxWh: page.energy.max
                    Text {
                        anchors.centerIn: parent
                        visible: page.energy.whIn <= 0 && page.energy.whOut <= 0
                        text: qsTr("No power readings in this range yet.")
                        color: Theme.faint
                    }
                }
                Text {
                    Layout.topMargin: 6
                    visible: (page.details.energy || []).length > 0
                    text: qsTr("Since new")
                    color: Theme.subtext
                    font.pixelSize: Theme.fontSizeSmall
                    font.weight: Font.DemiBold
                }
                GridLayout {
                    Layout.fillWidth: true
                    visible: (page.details.energy || []).length > 0
                    columns: Math.max(2, Math.min(5, Math.floor(width / 120)))
                    columnSpacing: 14
                    rowSpacing: 10
                    Repeater {
                        model: page.details.energy || []
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            spacing: 8
                            Icon {
                                Layout.alignment: Qt.AlignTop
                                Layout.topMargin: 2
                                name: modelData.icon
                                size: 14
                                color: Theme.faint
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.title
                                    color: Theme.faint
                                    font.pixelSize: Theme.fontSizeSmall
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: Format.energy(modelData.wh)
                                    color: Theme.text
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------------- chart
    Card {
        Layout.fillWidth: true
        visible: page.known
        implicitHeight: 330
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Flow {
                Layout.fillWidth: true
                spacing: 10
                Segmented {
                    compact: true
                    options: page.metrics
                    value: page.metric
                    onActivated: v => page.metric = v
                }
                Segmented {
                    compact: true
                    options: [
                        { value: "1h", text: qsTr("1 h") },
                        { value: "24h", text: qsTr("24 h") },
                        { value: "7d", text: qsTr("7 d") },
                        { value: "30d", text: qsTr("30 d") },
                    ]
                    value: page.range
                    onActivated: v => page.range = v
                }
            }
            RangeChart {
                Layout.fillWidth: true
                Layout.fillHeight: true
                points: page.chart.points
                points2: page.chartIn.points
                label: qsTr("Out")
                label2: qsTr("In")
                bands: page.chart.bands
                xFrom: page.now - page.spanMs
                xTo: page.now
                yUnit: page.metric === "runtime" ? qsTr("min")
                     : page.metric === "wattsOut" || page.metric === "wattsSolar" ? "W"
                     : page.metric === "temperature" ? "°C" : "%"
                yMax: page.metric === "charge" || page.metric === "load" ? 100
                    : page.chartTop(Math.max(page.chart.max || 0, page.chartIn.max || 0), yTicks)
                color: page.metric === "charge" || page.metric === "wattsOut" ? Theme.accent
                     : page.metric === "load" || page.metric === "wattsSolar" ? Theme.warning
                     : page.metric === "temperature" ? Theme.critical : Theme.success
                color2: Theme.success
                Text {
                    anchors.centerIn: parent
                    visible: parent.points.length === 0 && parent.points2.length === 0
                    text: qsTr("No readings in this range.")
                    color: Theme.faint
                }
            }
            Flow {
                Layout.fillWidth: true
                visible: page.metric === "wattsOut"
                spacing: 18
                Repeater {
                    model: [
                        { color: Theme.success, text: qsTr("Power in") },
                        { color: Theme.accent, text: qsTr("Power out") },
                    ]
                    delegate: Row {
                        required property var modelData
                        spacing: 6
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 14
                            height: 3
                            radius: 1.5
                            color: modelData.color
                        }
                        Text {
                            text: modelData.text
                            color: Theme.subtext
                            font.pixelSize: Theme.fontSizeSmall
                        }
                    }
                }
            }
        }
    }

    // ------------------------------------------------------ device settings
    ColumnLayout {
        id: settingsSection
        Layout.fillWidth: true
        visible: page.known && deviceSettings.available
        spacing: 12

        SectionHeader {
            text: qsTr("Device settings")
            detail: qsTr("nutty asks before it changes anything.")
        }

        Rectangle {
            Layout.fillWidth: true
            visible: deviceSettings.blockedReason.length > 0
            implicitHeight: blockedText.implicitHeight + 20
            radius: Theme.radius
            color: Theme.alpha(Theme.warning, Theme.dark ? 0.14 : 0.12)
            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                anchors.leftMargin: 14
                spacing: 10
                Icon {
                    name: "info"
                    size: 16
                    color: Theme.warning
                }
                Text {
                    id: blockedText
                    Layout.fillWidth: true
                    text: deviceSettings.blockedReason
                    color: Theme.text
                    wrapMode: Text.Wrap
                }
            }
        }

        Repeater {
            model: deviceSettings.groups
            delegate: Card {
                id: groupCard
                required property var modelData
                Layout.fillWidth: true
                padding: Theme.listPadding
                ColumnLayout {
                    width: parent.width
                    spacing: 0
                    Text {
                        Layout.leftMargin: Theme.rowInset
                        Layout.topMargin: 6
                        Layout.bottomMargin: 4
                        text: groupCard.modelData.title
                        color: Theme.subtext
                        font.pixelSize: Theme.fontSizeSmall
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 0.6
                    }
                    Repeater {
                        model: groupCard.modelData.controls
                        delegate: ControlRow {
                            id: controlRow
                            required property var modelData
                            required property int index
                            first: index === 0
                            control: deviceSettings.controls[controlRow.modelData]
                            onRequested: v => page.ask(controlRow, controlRow.control.id, v)
                        }
                    }
                }
            }
        }
    }

    GridLayout {
        Layout.fillWidth: true
        visible: page.known
        columns: page.contentWidthUsed > 900 ? 2 : 1
        columnSpacing: 16
        rowSpacing: 16

        // ------------------------------------------------------------ log
        Card {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            ColumnLayout {
                width: parent.width
                spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Activity")
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeLarge
                        font.weight: Font.DemiBold
                    }
                    PillButton {
                        kind: "ghost"
                        text: qsTr("History")
                        onClicked: page.navigate("history", "")
                    }
                }
                ActivityList {
                    Layout.fillWidth: true
                    interactive: false
                    showDevice: false
                    model: EventLogModel {
                        deviceId: page.deviceId
                        limit: 12
                    }
                }
            }
        }

        // ---------------------------------------------------------- rules
        Card {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            ColumnLayout {
                width: parent.width
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Power rules")
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeLarge
                        font.weight: Font.DemiBold
                    }
                    PillButton {
                        kind: "ghost"
                        iconName: "plus"
                        text: qsTr("Add rule")
                        onClicked: page.navigate("actions", "add:" + page.deviceId)
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: page.rules.length === 0
                    text: qsTr("No rules: nutty only notifies about this device.")
                    color: Theme.subtext
                    wrapMode: Text.Wrap
                }
                Repeater {
                    model: page.rules
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 10
                        Icon {
                            name: "shield"
                            size: 16
                            color: modelData.enabled ? Theme.accent : Theme.faint
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Text {
                                Layout.fillWidth: true
                                text: modelData.conditionText
                                color: modelData.enabled ? Theme.text : Theme.faint
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.description
                                color: Theme.subtext
                                font.pixelSize: Theme.fontSizeSmall
                                elide: Text.ElideRight
                            }
                        }
                    }
                }

                // ------------------------------------------------ raw values
                Text {
                    Layout.topMargin: 14
                    text: qsTr("Reported values")
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeLarge
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    visible: page.raw.length === 0
                    text: qsTr("Nothing reported yet.")
                    color: Theme.subtext
                }
                Repeater {
                    model: page.raw
                    delegate: RowLayout {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        spacing: 12
                        Text {
                            Layout.preferredWidth: 260
                            text: modelData.key
                            color: Theme.subtext
                            font.family: "monospace"
                            font.pixelSize: Theme.fontSizeSmall
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.value
                            color: Theme.text
                            font.family: "monospace"
                            font.pixelSize: Theme.fontSizeSmall
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }

    DeviceEditor {
        id: editor
    }

    DeviceControls {
        id: deviceSettings
        deviceId: page.deviceId
    }

    ControlConfirmSheet {
        id: confirmSetting
        objectName: "confirmSetting"
        onAccepted: {
            if (page.askedRow)
                page.askedRow.reset()
            deviceSettings.apply(page.askedId, page.askedValue)
        }
        onDeclined: {
            if (page.askedRow)
                page.askedRow.reset()
        }
    }

    DeviceSheet {
        id: deviceSheet
        editor: editor
    }

    ConfirmSheet {
        id: confirmRemove
        title: qsTr("Remove %1?").arg(page.info.name || "")
        message: qsTr("Its rules and its history go with it. This can't be undone.")
        onAccepted: {
            Catalog.removeDevice(page.deviceId)
            page.navigate("devices", "")
        }
    }
}
