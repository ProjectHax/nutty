// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtTest
import Nutty

// The device page's settings on a station with a few of them (Probe in
// tst_qml.cpp): every change asks first, risky ones twice, and nothing
// reaches the device without a yes to each question.
TestCase {
    id: tc
    name: "Settings"
    width: 1100
    height: 800
    visible: true
    when: windowShown

    property string stationId: ""

    Component { id: devicePage; DevicePage { anchors.fill: parent } }

    function initTestCase() {
        stationId = Probe.addStation()
        verify(stationId.length > 0)
    }

    function cleanupTestCase() {
        Probe.removeStation()
    }

    function init() {
        failOnWarning(/.*/)
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

    function openPage() {
        const page = createTemporaryObject(devicePage, tc, { deviceId: stationId })
        verify(page !== null)
        waitForRendering(page)
        return page
    }

    function rowFor(page, id) {
        const row = findItem(page, i => i.requested !== undefined && i.control !== undefined
                                        && i.control !== null && i.control.id === id)
        verify(row !== null, id)
        return row
    }

    // Brings `item` into view, so a click lands on it.
    function reveal(page, item) {
        page.scrollTo(item)
        wait(500)
    }

    function sheetOf(page) {
        const sheet = findChild(page, "confirmSetting")
        verify(sheet !== null)
        return sheet
    }

    function button(sheet, name) {
        const b = findChild(sheet.contentItem, name)
        verify(b !== null, name)
        return b
    }

    function test_settingsAndStats() {
        const page = openPage()
        rowFor(page, "acOutput")
        rowFor(page, "chargeLimit")
        rowFor(page, "screenTimeout")
        verify(findItem(page, i => i.text === "Power now") !== null)
        verify(findItem(page, i => i.text === "Main battery") !== null)
        verify(findItem(page, i => i.text === "Device settings") !== null)
        page.contentY = Math.max(0, page.contentHeight - page.height)
        waitForRendering(page)
    }

    function test_oneQuestion() {
        const page = openPage()
        const sent = Probe.applied().length
        const row = rowFor(page, "chargeLimit")
        row.requested(80)
        const sheet = sheetOf(page)
        tryVerify(() => sheet.opened)
        verify(!sheet.question.twice)
        verify(sheet.title.indexOf("Shed station") >= 0, sheet.title)
        compare(Probe.applied().length, sent, "sent before the user said yes")

        mouseClick(button(sheet, "confirmFirst"))
        tryVerify(() => !sheet.visible)
        compare(Probe.applied().length, sent + 1)
        compare(Probe.applied()[sent].id, "chargeLimit")
        compare(Probe.applied()[sent].value, 80)
        verify(row.control.pending)

        Probe.finish("chargeLimit", 80, true)
        tryVerify(() => row.control.result === "Applied")
        compare(row.control.value, 80)
    }

    function test_cancelSendsNothing() {
        const page = openPage()
        const sent = Probe.applied().length
        const row = rowFor(page, "screenTimeout")
        const sheet = sheetOf(page)

        row.requested(60)
        tryVerify(() => sheet.opened)
        mouseClick(button(sheet, "confirmCancel"))
        tryVerify(() => !sheet.visible)

        row.requested(10)
        tryVerify(() => sheet.opened)
        keyClick(Qt.Key_Escape)
        tryVerify(() => !sheet.visible)

        compare(Probe.applied().length, sent)
        compare(row.control.value, 30)
    }

    function test_riskyAsksTwice() {
        const page = openPage()
        const sent = Probe.applied().length
        const row = rowFor(page, "acOutput")
        const toggle = findItem(row, i => i.checkable === false && i.checked !== undefined && i.visible)
        verify(toggle !== null)
        const wasOn = toggle.checked
        reveal(page, row)

        // A real click on the switch only asks.
        mouseClick(toggle)
        const sheet = sheetOf(page)
        tryVerify(() => sheet.opened)
        verify(sheet.question.twice)
        compare(sheet.step, 0)
        compare(toggle.checked, wasOn)

        mouseClick(button(sheet, "confirmFirst"))
        compare(sheet.step, 1)
        verify(sheet.visible)
        verify(sheet.subtitle.length > 0)
        compare(Probe.applied().length, sent, "sent after one yes")

        // The second yes wakes up after a moment, so a double click can't
        // answer both questions.
        const yes = button(sheet, "confirmSecond")
        verify(!yes.enabled)
        waitForRendering(sheet.contentItem)
        const spot = yes.mapToItem(sheet.contentItem, yes.width / 2, yes.height / 2)
        mouseClick(sheet.contentItem, spot.x, spot.y)
        compare(Probe.applied().length, sent)
        verify(sheet.visible)
        tryVerify(() => yes.enabled)
        mouseClick(yes)
        tryVerify(() => !sheet.visible)
        compare(Probe.applied().length, sent + 1)
        compare(Probe.applied()[sent].id, "acOutput")
        compare(Probe.applied()[sent].value, wasOn ? 0 : 1)

        // The switch shows what the device reports, not what was asked for.
        compare(toggle.checked, wasOn)
        Probe.finish("acOutput", wasOn ? 0 : 1, true)
        tryVerify(() => toggle.checked !== wasOn)
    }

    function test_secondQuestionCancel() {
        const page = openPage()
        const sent = Probe.applied().length
        const row = rowFor(page, "acOutput")
        const value = row.control.value === 1 ? 0 : 1
        row.requested(value)
        const sheet = sheetOf(page)
        tryVerify(() => sheet.opened)
        mouseClick(button(sheet, "confirmFirst"))
        compare(sheet.step, 1)
        mouseClick(button(sheet, "confirmCancel"))
        tryVerify(() => !sheet.visible)
        compare(Probe.applied().length, sent)
    }

    // A double click on the first question's button lands on the second
    // question's "Cancel".
    function test_doubleClickCancels() {
        const page = openPage()
        const sent = Probe.applied().length
        const row = rowFor(page, "acOutput")
        row.requested(row.control.value === 1 ? 0 : 1)
        const sheet = sheetOf(page)
        tryVerify(() => sheet.opened)
        waitForRendering(sheet.contentItem)
        const first = button(sheet, "confirmFirst")
        const spot = first.mapToItem(sheet.contentItem, first.width / 2, first.height / 2)
        mouseClick(sheet.contentItem, spot.x, spot.y)
        wait(80)  // a quick double click, a few frames apart
        mouseClick(sheet.contentItem, spot.x, spot.y)
        tryVerify(() => !sheet.visible)
        compare(Probe.applied().length, sent)
    }

    function test_unconfirmedShows() {
        const page = openPage()
        const row = rowFor(page, "screenTimeout")
        row.requested(60)
        const sheet = sheetOf(page)
        tryVerify(() => sheet.opened)
        mouseClick(button(sheet, "confirmFirst"))
        tryVerify(() => !sheet.visible)
        Probe.finish("screenTimeout", 60, false)
        tryVerify(() => row.control.result === "Unconfirmed")
        verify(row.failed)
        compare(row.control.value, 30)
    }
}
