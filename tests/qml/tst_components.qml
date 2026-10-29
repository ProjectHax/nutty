// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Window
import QtTest
import Nutty

// Instantiates every component with typical and edge-case data. Any QML
// warning (binding loop, type error, missing image, ...) fails the test.
TestCase {
    id: tc
    name: "Components"
    width: 1000
    height: 700
    visible: true
    when: windowShown

    function init() {
        failOnWarning(/.*/)
    }

    ListModel {
        id: events
        ListElement { timestamp: 1767225600000; type: "OnBattery"; severity: "Warning"; deviceName: "UPS"; title: "On battery"; message: "Input power lost."; day: "Today" }
        ListElement { timestamp: 1767222000000; type: "PowerRestored"; severity: "Info"; deviceName: "UPS"; title: "Power restored"; message: ""; day: "Today" }
        ListElement { timestamp: 1767132000000; type: "CommLost"; severity: "Critical"; deviceName: ""; title: "Connection lost"; message: "No response."; day: "Yesterday" }
    }
    ListModel { id: noEvents }

    Component { id: softShadow; SoftShadow { width: 200; height: 100 } }
    Component { id: card; Card { width: 200; height: 120; hoverable: true; clickable: true; Text { text: "Body" } } }
    Component { id: icon; Icon { name: "bolt"; size: 24; color: "#ff0000" } }
    Component { id: missingIcon; Icon { name: "no-such-icon" } }
    Component { id: animatedNumber; AnimatedNumber { value: 42; decimals: 1; suffix: " %" } }
    Component { id: iconButton; IconButton { iconName: "gear"; toolTip: "Settings" } }
    Component { id: pillButton; PillButton { text: "Go"; iconName: "check" } }
    Component { id: statusChip; StatusChip { status: "onBattery" } }
    Component { id: battery; BatteryVisual { width: 120; height: 56; level: 63 } }
    Component { id: batteryVertical; BatteryVisual { width: 50; height: 100; vertical: true; level: 100; charging: true } }
    Component { id: batteryOffline; BatteryVisual { width: 120; height: 56; offline: true } }
    Component { id: batteryStale; BatteryVisual { width: 120; height: 56; level: 30; stale: true } }
    Component { id: arcGauge; ArcGauge { width: 120; height: 120; value: 63; caption: "Charge" } }
    Component { id: arcGaugeFull; ArcGauge { width: 120; height: 120; value: 10; maximum: 60; sweepDegrees: 360; valueText: "10"; unit: "" } }
    Component { id: arcGaugeUnknown; ArcGauge { width: 120; height: 120 } }
    Component { id: sparkline; Sparkline { width: 160; height: 40; values: [10, 40, 30, 80, 60] } }
    Component { id: sparklineEmpty; Sparkline { width: 160; height: 40 } }
    Component {
        id: rangeChart
        RangeChart {
            width: 480; height: 220
            points: [{ x: 0, y: 100 }, { x: 3600000, y: 80 }, { x: 7200000, y: 95 }]
            bands: [{ from: 1000000, to: 3600000 }]
        }
    }
    Component { id: rangeChartEmpty; RangeChart { width: 480; height: 220 } }
    Component { id: summaryBanner; SummaryBanner { width: 600; level: "critical"; title: "Low battery"; subtitle: "12 %"; actionText: "Cancel" } }
    Component { id: summaryOk; SummaryBanner { width: 600; title: "All good" } }
    Component { id: deviceArt; DeviceArt { width: 96; height: 96; kind: "RackUps" } }
    Component {
        id: deviceCard
        DeviceCard {
            width: 340
            name: "Office UPS"; subtitle: "NUT"; kind: "TowerUps"; status: "onBattery"
            charge: 63; runtime: 1260; load: 41; wattsOut: 388; lastUpdated: Date.now() - 5000
            history: [100, 98, 90, 80, 63]
        }
    }
    Component { id: deviceCardEmpty; DeviceCard { width: 340; name: "New device" } }
    Component { id: eventRow; EventRow { width: 400; type: "LowBattery"; severity: "Critical"; title: "Low"; timestamp: Date.now() } }
    Component { id: activityList; ActivityList { width: 400; interactive: false; model: events } }
    Component { id: activityEmpty; ActivityList { width: 400; height: 300; model: noEvents } }
    Component { id: navItem; NavItem { text: "Home"; iconName: "home"; selected: true; badge: "3" } }
    Component {
        id: sidebar
        Sidebar {
            height: 600
            items: [{ key: "home", text: "Home", icon: "home" }, { key: "history", text: "History", icon: "history", badge: 2 }]
            footerItems: [{ key: "about", text: "About", icon: "info" }]
            currentKey: "home"
        }
    }
    Component { id: windowButton; WindowButton { role: "maximize"; style: "breeze" } }
    Component { id: titleBar; TitleBar { width: 600; title: "Home"; showWindowButtons: true; showBack: true } }
    Component { id: titleBarLeft; TitleBar { width: 600; title: "Home"; buttonLayout: "close,minimize,maximize:"; style: "breeze"; showWindowButtons: true } }
    Component { id: countdownPanel; CountdownPanel { width: 380; deviceName: "UPS"; reason: "Battery below 20 %"; remaining: 42; total: 60 } }
    Component { id: framedWindow; Window { width: 400; height: 300; WindowFrame { frameless: true; Text { text: "x" } } } }
    Component { id: countdownWindow; CountdownWindow { visible: false; deviceName: "UPS"; remaining: 5; total: 60 } }
    Component { id: resizeFrame; ResizeFrame { } }

    function test_create_data() {
        return [
            { tag: "SoftShadow", component: softShadow },
            { tag: "Card", component: card },
            { tag: "Icon", component: icon },
            { tag: "Icon (missing)", component: missingIcon },
            { tag: "AnimatedNumber", component: animatedNumber },
            { tag: "IconButton", component: iconButton },
            { tag: "PillButton", component: pillButton },
            { tag: "StatusChip", component: statusChip },
            { tag: "BatteryVisual", component: battery },
            { tag: "BatteryVisual (vertical)", component: batteryVertical },
            { tag: "BatteryVisual (offline)", component: batteryOffline },
            { tag: "BatteryVisual (stale)", component: batteryStale },
            { tag: "ArcGauge", component: arcGauge },
            { tag: "ArcGauge (360)", component: arcGaugeFull },
            { tag: "ArcGauge (unknown)", component: arcGaugeUnknown },
            { tag: "Sparkline", component: sparkline },
            { tag: "Sparkline (empty)", component: sparklineEmpty },
            { tag: "RangeChart", component: rangeChart },
            { tag: "RangeChart (empty)", component: rangeChartEmpty },
            { tag: "SummaryBanner", component: summaryBanner },
            { tag: "SummaryBanner (ok)", component: summaryOk },
            { tag: "DeviceArt", component: deviceArt },
            { tag: "DeviceCard", component: deviceCard },
            { tag: "DeviceCard (empty)", component: deviceCardEmpty },
            { tag: "EventRow", component: eventRow },
            { tag: "ActivityList", component: activityList },
            { tag: "ActivityList (empty)", component: activityEmpty },
            { tag: "NavItem", component: navItem },
            { tag: "Sidebar", component: sidebar },
            { tag: "WindowButton", component: windowButton },
            { tag: "TitleBar", component: titleBar },
            { tag: "TitleBar (left buttons)", component: titleBarLeft },
            { tag: "CountdownPanel", component: countdownPanel },
            { tag: "WindowFrame", component: framedWindow },
            { tag: "CountdownWindow", component: countdownWindow },
            { tag: "ResizeFrame", component: resizeFrame },
        ]
    }

    function test_create(data) {
        const obj = createTemporaryObject(data.component, tc)
        verify(obj !== null, "could not create " + data.tag)
        wait(60)
    }

    function test_gallery_data() {
        return [{ tag: "home", page: "home" }, { tag: "components", page: "components" }, { tag: "art", page: "art" }]
    }

    function test_gallery(data) {
        const c = Qt.createComponent("fixtures/Gallery.qml")
        compare(c.status, Component.Ready, c.errorString())
        const win = createTemporaryObject(c, tc, { page: data.page, width: 1100, height: 760 })
        verify(win !== null)
        wait(150)
        win.page = "home"
        wait(50)
    }

    function test_statusChipLabels() {
        const chip = createTemporaryObject(statusChip, tc)
        for (const s of ["online", "charging", "onBattery", "lowBattery", "fsd", "bypass", "overload",
                         "replaceBattery", "offline", "stale", "error", "unknown", "bogus"]) {
            chip.status = s
            verify(chip.label(s).length > 0)
        }
        compare(chip.label("bogus"), chip.label("unknown"))
        chip.text = "Custom"
        compare(chip.Accessible.name, "Custom")
    }

    function test_titleBarLayout() {
        const bar = createTemporaryObject(titleBar, tc)
        // Only Linux has a default layout; the bar is Linux-only.
        bar.buttonLayout = "appmenu:minimize,maximize,close"
        compare(bar.leftButtons, [])
        compare(bar.rightButtons, ["minimize", "maximize", "close"])
        bar.buttonLayout = "close,minimize:appmenu"
        compare(bar.leftButtons, ["close", "minimize"])
        compare(bar.rightButtons, [])
        bar.buttonLayout = "icon:close"
        compare(bar.rightButtons, ["close"])
    }

    function test_batteryFraction() {
        const b = createTemporaryObject(battery, tc)
        compare(b.fraction, 0.63)
        b.level = 140
        compare(b.fraction, 1)
        b.level = -1
        verify(!b.known)
        compare(b.fraction, 0)
        b.level = 50
        b.offline = true
        verify(!b.known)
    }

    function test_cardClicked() {
        const c = createTemporaryObject(card, tc)
        const spy = createTemporaryObject(spyComponent, tc, { target: c, signalName: "clicked" })
        mouseClick(c)
        compare(spy.count, 1)
    }

    function test_sidebarActivated() {
        const s = createTemporaryObject(sidebar, tc)
        const spy = createTemporaryObject(spyComponent, tc, { target: s, signalName: "activated" })
        waitForRendering(s)
        const item = findChild(s, "nav-history")
        verify(item !== null)
        mouseClick(item)
        compare(spy.count, 1)
        compare(spy.signalArguments[0][0], "history")
    }

    function test_countdownCancel() {
        const p = createTemporaryObject(countdownPanel, tc)
        const spy = createTemporaryObject(spyComponent, tc, { target: p, signalName: "cancelRequested" })
        waitForRendering(p)
        compare(p.clock(42), "42")
        compare(p.clock(125), "2:05")
        const button = findChild(p, "cancelButton")
        verify(button !== null)
        mouseClick(button)
        compare(spy.count, 1)
    }

    function test_theme() {
        verify(Theme.radius > 0)
        compare(Theme.alpha("#ff0000", 0.5).a, 0.5)
        verify(Theme.levelColor(10) !== Theme.levelColor(90))
        const mode = Theme.mode
        Theme.mode = "Dark"
        verify(Theme.dark)
        Theme.mode = "Light"
        verify(!Theme.dark)
        Theme.mode = mode
    }

    function test_format() {
        compare(Format.duration(-1), "—")
        compare(Format.duration(45), "45 s")
        compare(Format.duration(1260), "21 min")
        compare(Format.duration(5040), "1 h 24 min")
        verify(Format.watts(1500).indexOf("kW") > 0)
        compare(Format.relativeTime(Date.now() - 1000, Date.now()), "just now")
    }

    Component { id: spyComponent; SignalSpy { } }
}
