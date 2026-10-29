// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Layouts
import Nutty

// What happened, when: charts over a chosen range, the outages, and the
// full event log with filters and CSV export.
ScrollPage {
    id: page

    property string range: "24h"
    property string deviceFilter: ""  // empty = all devices
    property string category: "all"
    property string metric: "charge"
    readonly property real spanMs: ({ "1h": 3600, "24h": 86400, "7d": 7 * 86400, "30d": 30 * 86400 })[range] * 1000
    readonly property real now: Series.revision, Series.now()
    // The log's range starts when the range is picked, so paging and live
    // inserts aren't reset every time the charts refresh.
    property real anchorNow: Series.now()
    readonly property date fromDate: new Date(anchorNow - spanMs)
    onRangeChanged: anchorNow = Series.now()
    readonly property string chartDevice: deviceFilter.length > 0 ? deviceFilter
                                        : Catalog.devices.length > 0 ? Catalog.devices[0].id : ""
    readonly property var chart: Series.revision, Series.get(chartDevice, metric, now - spanMs, now, 300)
    readonly property var metricInfo: ({
        charge: { unit: "%", min: 0, max: 100 },
        runtime: { unit: qsTr("min"), min: 0, max: -1 },
        load: { unit: "%", min: 0, max: 100 },
        wattsOut: { unit: "W", min: 0, max: -1 },
        inputVoltage: { unit: "V", min: -1, max: -1 },
    })[metric]

    function niceMax(v) {
        if (!(v > 0))
            return 10
        const step = Math.pow(10, Math.floor(Math.log(v) / Math.LN10))
        return Math.ceil(v * 1.1 / step) * step
    }

    EventLogModel {
        id: log
        deviceId: page.deviceFilter
        category: page.category
        from: page.fromDate
        search: searchField.text
        pageSize: 50
    }

    EpisodeModel {
        id: outages
        deviceId: page.deviceFilter
        from: page.fromDate
        pageSize: 20
    }

    // ---------------------------------------------------------------- filters
    Flow {
        Layout.fillWidth: true
        spacing: 10
        Segmented {
            options: [
                { value: "1h", text: qsTr("1 hour") },
                { value: "24h", text: qsTr("24 hours") },
                { value: "7d", text: qsTr("7 days") },
                { value: "30d", text: qsTr("30 days") },
            ]
            value: page.range
            onActivated: v => page.range = v
        }
        Dropdown {
            width: 210
            options: {
                const list = [{ value: "", text: qsTr("All devices") }]
                for (const d of Catalog.devices)
                    list.push({ value: d.id, text: d.name })
                return list
            }
            value: page.deviceFilter
            onPicked: v => page.deviceFilter = v
        }
    }

    // ------------------------------------------------------------------ chart
    Card {
        Layout.fillWidth: true
        implicitHeight: 340
        visible: Catalog.devices.length > 0
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Text {
                    Layout.fillWidth: true
                    text: Catalog.deviceName(page.chartDevice)
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeLarge
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Segmented {
                    compact: true
                    options: [
                        { value: "charge", text: qsTr("Charge") },
                        { value: "runtime", text: qsTr("Runtime") },
                        { value: "load", text: qsTr("Load") },
                        { value: "wattsOut", text: qsTr("Power") },
                        { value: "inputVoltage", text: qsTr("Input") },
                    ]
                    value: page.metric
                    onActivated: v => page.metric = v
                }
            }
            RangeChart {
                Layout.fillWidth: true
                Layout.fillHeight: true
                points: page.chart.points
                bands: page.chart.bands
                xFrom: page.now - page.spanMs
                xTo: page.now
                yUnit: page.metricInfo.unit
                yMin: page.metricInfo.min >= 0 ? page.metricInfo.min
                    : page.chart.min !== undefined ? Math.max(0, Math.floor(page.chart.min * 0.95)) : 0
                yMax: page.metricInfo.max >= 0 ? page.metricInfo.max : page.niceMax(page.chart.max)
                color: page.metric === "charge" ? Theme.accent : page.metric === "load" ? Theme.warning : Theme.success
                Text {
                    anchors.centerIn: parent
                    visible: parent.points.length === 0
                    text: qsTr("No readings in this range.")
                    color: Theme.faint
                }
            }
        }
    }

    // ---------------------------------------------------------------- outages
    SectionHeader {
        text: qsTr("Outages")
        detail: outages.total > 0 ? qsTr("%n on battery", "", outages.total) + " · " + Format.duration(outages.totalSeconds) : ""
        PillButton {
            kind: "ghost"
            iconName: "export"
            text: qsTr("Export")
            visible: outages.total > 0
            onClicked: {
                const url = AppInfo.saveFileUrl(qsTr("Export outages"), "nutty-outages.csv", qsTr("CSV files (*.csv)"))
                if (url.toString().length > 0)
                    outages.exportCsv(url)
            }
        }
    }

    Card {
        Layout.fillWidth: true
        ColumnLayout {
            width: parent.width
            spacing: 0
            Text {
                Layout.fillWidth: true
                Layout.margins: 8
                visible: outages.count === 0
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("No time on battery in this range.")
                color: Theme.subtext
            }
            Repeater {
                model: outages
                delegate: OutageRow {
                    Layout.fillWidth: true
                    first: index === 0
                    deviceName: model.deviceName
                    start: model.start
                    ongoing: model.ongoing
                    duration: model.duration
                    startCharge: model.startCharge
                    minCharge: model.minCharge
                    minRuntime: model.minRuntime
                    actions: model.actions
                    onClicked: page.navigate("device", model.deviceId)
                }
            }
            PillButton {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 8
                visible: outages.hasMore
                kind: "ghost"
                text: qsTr("Show more")
                onClicked: outages.loadMore()
            }
        }
    }

    // -------------------------------------------------------------------- log
    SectionHeader {
        text: qsTr("Event log")
        detail: qsTr("%n event(s)", "", log.total)
        PillButton {
            kind: "ghost"
            iconName: "export"
            text: qsTr("Export CSV")
            visible: log.total > 0
            onClicked: {
                const url = AppInfo.saveFileUrl(qsTr("Export events"), "nutty-events.csv", qsTr("CSV files (*.csv)"))
                if (url.toString().length > 0)
                    log.exportCsv(url)
            }
        }
    }

    Flow {
        Layout.fillWidth: true
        spacing: 10
        Segmented {
            compact: true
            options: [
                { value: "all", text: qsTr("All") },
                { value: "power", text: qsTr("Power") },
                { value: "battery", text: qsTr("Battery") },
                { value: "connection", text: qsTr("Connection") },
                { value: "actions", text: qsTr("Actions") },
            ]
            value: page.category
            onActivated: v => page.category = v
        }
        InputField {
            id: searchField
            width: 240
            height: 30
            placeholderText: qsTr("Search events")
        }
    }

    Card {
        Layout.fillWidth: true
        ColumnLayout {
            width: parent.width
            spacing: 4
            ActivityList {
                Layout.fillWidth: true
                interactive: false
                showDevice: page.deviceFilter.length === 0
                model: log
                emptyText: qsTr("No events match these filters.")
            }
            PillButton {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 6
                visible: log.hasMore
                kind: "secondary"
                text: qsTr("Load older events")
                onClicked: log.loadMore()
            }
        }
    }
}
