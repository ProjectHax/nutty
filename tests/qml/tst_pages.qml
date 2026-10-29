// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Controls
import QtTest
import Nutty

// Loads every page, the main window and the editors on the real view-models
// (tst_qml.cpp provides a Monitor with fake devices and in-memory history).
// Any QML warning fails the test.
TestCase {
    id: tc
    name: "Pages"
    width: 1100
    height: 760
    visible: true
    when: windowShown

    function init() {
        failOnWarning(/.*/)
    }

    readonly property string rackId: Catalog.devices.length > 0 ? Catalog.devices[0].id : ""

    Component { id: homePage; HomePage { anchors.fill: parent } }
    Component { id: historyPage; HistoryPage { anchors.fill: parent } }
    Component { id: devicePage; DevicePage { anchors.fill: parent; deviceId: tc.rackId } }
    Component { id: devicesPage; DevicesPage { anchors.fill: parent } }
    Component { id: notificationsPage; NotificationsPage { anchors.fill: parent } }
    Component { id: actionsPage; ActionsPage { anchors.fill: parent } }
    Component { id: generalPage; GeneralPage { anchors.fill: parent } }
    Component { id: aboutPage; AboutPage { anchors.fill: parent } }
    Component { id: mainWindow; Main { visible: true } }
    Component { id: countdown; CountdownAlert { visible: true } }
    Component { id: deviceSheet; DeviceSheet { editor: DeviceEditor {} } }

    function test_services() {
        compare(Catalog.devices.length, 2)
        compare(Devices.count, 2)
        verify(rackId.length > 0)
    }

    function test_pages_data() {
        return [
            { tag: "home", component: homePage },
            { tag: "history", component: historyPage },
            { tag: "device", component: devicePage },
            { tag: "devices", component: devicesPage },
            { tag: "notifications", component: notificationsPage },
            { tag: "actions", component: actionsPage },
            { tag: "general", component: generalPage },
            { tag: "about", component: aboutPage },
        ]
    }

    function test_pages(data) {
        const page = createTemporaryObject(data.component, tc)
        verify(page !== null, data.tag)
        waitForRendering(page)
        verify(page.contentHeight > 0)
        // Scroll to the end so lazily laid-out sections get exercised too.
        page.contentY = Math.max(0, page.contentHeight - page.height)
        waitForRendering(page)
    }

    function test_mainWindowVisitsEveryPage() {
        const win = createTemporaryObject(mainWindow, tc)
        verify(win !== null)
        for (const p of ["home", "history", "devices", "notifications", "actions", "general", "about"]) {
            win.openPage(p, "")
            compare(win.page, p)
            wait(50)
        }
        win.openPage("device", rackId)
        compare(win.page, "device")
        // A device page without an id stays where it is.
        win.openPage("device", "")
        compare(win.page, "device")
        win.openPage("nowhere", "")
        compare(win.page, "home")
        wait(50)
    }

    // Every open sheet is as tall as its content, up to the window's height.
    function verifyOpenSheets(expected) {
        const overlay = Overlay.overlay
        let open = 0
        for (let i = 0; i < overlay.children.length; ++i) {
            const item = overlay.children[i]
            if (!item.visible || !item.toString().startsWith("QQuickPopupItem"))
                continue
            ++open
            verify(item.implicitHeight > 100, "sheet has no implicit height")
            compare(item.height, Math.min(item.implicitHeight, overlay.height - 32))
        }
        compare(open, expected)
    }

    function test_editorsOpen() {
        const page = createTemporaryObject(actionsPage, tc, { request: "add:" + rackId })
        verify(page !== null)
        waitForRendering(page)
        wait(100)
        verifyOpenSheets(1)

        const devices = createTemporaryObject(devicesPage, tc, { request: "add" })
        verify(devices !== null)
        waitForRendering(devices)
        wait(100)
        verifyOpenSheets(2)

        const channels = createTemporaryObject(notificationsPage, tc)
        verify(channels !== null)
        channels.addChannel()
        waitForRendering(channels)
        wait(100)
        verifyOpenSheets(3)
    }

    function findItem(item, predicate) {
        if (!item)
            return null
        if (predicate(item))
            return item
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) {
            const found = findItem(kids[i], predicate)
            if (found)
                return found
        }
        return null
    }

    // A click in a sheet must not also reach the page under it (a TapHandler
    // with a passive grab lets the press through).
    function test_sheetKeepsItsClicks() {
        const win = createTemporaryObject(mainWindow, tc, { width: 900, height: 600 })
        win.openPage("devices", "add")
        waitForRendering(win.contentItem)
        wait(300)  // the sheet's enter transition
        const card = findItem(win.contentItem,
                              i => i.clickable === true && i.modelData !== undefined && i.modelData.key === "Nut")
        const row = findItem(win.contentItem,
                             i => typeof i.deviceId === "string" && i.deviceId.length > 0 && i.height > 0 && i.height < 120)
        verify(card !== null && row !== null)
        const c = card.mapToItem(win.contentItem, 0, 0)
        const r = row.mapToItem(win.contentItem, 0, 0)
        const top = Math.max(c.y, r.y)
        const bottom = Math.min(c.y + card.height, r.y + row.height)
        verify(bottom > top, "the NUT card doesn't cover a device row")

        mouseClick(card, card.width / 2, (top + bottom) / 2 - c.y)
        wait(100)
        compare(win.page, "devices")
        verify(!card.visible, "the sheet didn't move on to the NUT form")
    }

    function findAll(item, predicate, out) {
        out = out || []
        if (!item)
            return out
        if (predicate(item))
            out.push(item)
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i)
            findAll(kids[i], predicate, out)
        return out
    }

    // The rack is online at 100 % and the desk on battery at 62 %: chips of
    // different lengths next to charges of different lengths.
    function test_deviceRowsLineUp() {
        const win = createTemporaryObject(mainWindow, tc, { width: 1200, height: 600 })
        win.openPage("devices", "")
        waitForRendering(win.contentItem)
        const inRow = i => i.parent && i.parent.parent && typeof i.parent.parent.deviceId === "string"
        const chips = findAll(win.contentItem, i => i.alarming !== undefined && i.visible && inRow(i))
        const charges = findAll(win.contentItem,
                                i => typeof i.text === "string" && i.text.indexOf("%") > 0 && i.visible && inRow(i))
        compare(chips.length, 2)
        compare(charges.length, 2)
        const left = i => i.mapToItem(win.contentItem, 0, 0).x
        verify(chips[0].status !== chips[1].status)
        compare(left(chips[0]), left(chips[1]))
        compare(chips[0].width, chips[1].width)
        compare(left(charges[0]) + charges[0].width, left(charges[1]) + charges[1].width)
        compare(charges[0].horizontalAlignment, Text.AlignRight)
    }

    // Settings, channels and rules are all ListRows: their icons and titles
    // start at the same place on every page, and a row is as tall as the
    // others whatever control it holds.
    function test_listRowsLineUp() {
        const win = createTemporaryObject(mainWindow, tc, { width: 1100, height: 700 })
        const rowsOf = page => {
            win.openPage(page, "")
            waitForRendering(win.contentItem)
            return findAll(win.contentItem, i => i.elideTitle !== undefined && i.stacked !== undefined
                                                && i.visible && i.width > 0)
        }
        const leftOf = (row, predicate) => {
            const item = findAll(row, predicate)[0]
            return item ? item.mapToItem(win.contentItem, 0, 0).x : -1
        }
        const iconLeft = row => leftOf(row, i => i.name === row.iconName && i.size === 20)
        const titleLeft = row => leftOf(row, i => i.text === row.title && i.font.weight === Font.DemiBold)

        let icon = -1
        let title = -1
        for (const page of ["general", "actions", "notifications", "about"]) {
            const rows = rowsOf(page)
            verify(rows.length > 0, page + " has no rows")
            for (const row of rows) {
                const what = page + ": " + row.title
                if (icon < 0) {
                    icon = iconLeft(row)
                    title = titleLeft(row)
                }
                compare(iconLeft(row), icon, what + " icon")
                compare(titleLeft(row), title, what + " title")
                verify(row.height >= Theme.rowHeight, what + " is " + row.height + " tall")
                if (row.description.length === 0)
                    compare(row.height, Theme.rowHeight, what + " height")
            }
        }
    }

    function test_backGoesToThePreviousPage() {
        const win = createTemporaryObject(mainWindow, tc, { width: 1000, height: 600 })
        waitForRendering(win.contentItem)
        const back = findChild(win.contentItem, "backButton")
        verify(back !== null)
        verify(!back.visible, "nothing to go back to yet")

        // From the device list...
        win.openPage("devices", "")
        win.openPage("device", rackId)
        verify(back.visible)
        mouseClick(back)
        compare(win.page, "devices")

        // ...or from home, by the mouse's own buttons.
        win.openPage("home", "")
        win.openPage("device", rackId)
        mouseClick(win.contentItem, win.width / 2, win.height / 2, Qt.BackButton)
        compare(win.page, "home")
        mouseClick(win.contentItem, win.width / 2, win.height / 2, Qt.ForwardButton)
        compare(win.page, "device")
        compare(win.deviceId, rackId)

        // And back through the earlier pages, one at a time.
        win.goBack()
        compare(win.page, "home")
        win.goBack()
        compare(win.page, "devices")

        // A request such as "add" isn't replayed on the way back.
        win.openPage("notifications", "add")
        win.openPage("home", "")
        win.goBack()
        compare(win.page, "notifications")
        compare(win.deviceId, "")
    }

    // Clicking a row of the device list, then the mouse's back button with
    // the pointer still where the row was.
    // The back button sits on the title's line, centred on its capitals.
    function test_backButtonOnTheTitleLine() {
        const win = createTemporaryObject(mainWindow, tc, { width: 1000, height: 600 })
        const back = findChild(win.contentItem, "backButton")
        let offset = undefined
        for (const p of ["home", "history"]) {
            win.openPage("devices", "")
            win.openPage(p, "")
            waitForRendering(win.contentItem)
            const title = findItem(back.parent, i => i.font !== undefined && i.font.pixelSize === Theme.fontSizeTitle)
            verify(title !== null, p)
            const middle = back.mapToItem(title, 0, back.height / 2).y
            // Centred on the capitals, measured here in the title's font.
            const metrics = Qt.createQmlObject("import QtQuick; FontMetrics {}", tc)
            metrics.font = title.font
            const capitals = title.baselineOffset - metrics.tightBoundingRect("H").height / 2
            metrics.destroy()
            verify(Math.abs(middle - capitals) <= 1, p + ": button at " + middle + ", capitals at " + capitals)
            if (offset === undefined)
                offset = middle
            compare(middle, offset, p)
        }
    }

    function test_mouseBackFromTheDeviceList() {
        const win = createTemporaryObject(mainWindow, tc, { width: 1000, height: 600 })
        win.openPage("devices", "")
        waitForRendering(win.contentItem)
        const row = findItem(win.contentItem,
                             i => typeof i.deviceId === "string" && i.deviceId.length > 0 && i.height > 0 && i.height < 120)
        verify(row !== null)
        const p = row.mapToItem(win.contentItem, 300, row.height / 2)
        mouseClick(win.contentItem, p.x, p.y)
        tryCompare(win, "page", "device")
        waitForRendering(win.contentItem)
        mouseClick(win.contentItem, p.x, p.y, Qt.BackButton)
        compare(win.page, "devices")

        // Not while a dialog is open.
        win.openPage("devices", "add")
        wait(300)  // the sheet's enter transition
        verify(win.dialogOpen)
        mouseClick(win.contentItem, 20, win.height - 20, Qt.BackButton)
        compare(win.page, "devices")
    }

    function test_keyboardBack() {
        const backKeys = Qt.platform.os === "osx" ? "Ctrl+[" : "Alt+Left"
        const win = createTemporaryObject(mainWindow, tc, { width: 1000, height: 600 })
        win.requestActivate()
        tryVerify(() => win.active)
        win.openPage("home", "")
        win.openPage("devices", "")
        waitForRendering(win.contentItem)

        // Backspace is for text, not for leaving the page.
        keyClick(Qt.Key_Backspace)
        compare(win.page, "devices")
        keySequence(backKeys)
        compare(win.page, "home")
        // The Back key some mice and keyboards have.
        keyClick(Qt.Key_Forward)
        compare(win.page, "devices")
        keyClick(Qt.Key_Back)
        compare(win.page, "home")

        // Not while a dialog is open...
        win.openPage("devices", "add")
        wait(300)  // the sheet's enter transition
        keySequence(backKeys)
        compare(win.page, "devices")

        // ...and again once it's closed.
        keyClick(Qt.Key_Escape)
        wait(300)
        keySequence(backKeys)
        compare(win.page, "home")
    }

    // tst_qml.cpp makes "This computer" find one device with a very long name.
    function test_discoveredDeviceFits() {
        const sheet = createTemporaryObject(deviceSheet, tc)
        const editor = sheet.editor
        editor.reset("System")
        sheet.typeChosen = true
        sheet.open()
        editor.discover()
        tryVerify(() => editor.discovered.length === 1)
        waitForRendering(sheet.contentItem)

        const use = findChild(sheet.contentItem, "useDiscovered")
        verify(use !== null)
        const row = use.parent.parent
        const right = use.mapToItem(row, use.width, 0).x
        verify(right <= row.width, "\"Use\" ends at " + right + " in a row " + row.width + " wide")
    }

    function test_countdown() {
        verify(!Actions.pending)
        const win = createTemporaryObject(countdown, tc)
        verify(win !== null)
        wait(50)
    }
}
