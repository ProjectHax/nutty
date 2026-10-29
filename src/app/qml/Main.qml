// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Nutty

// The main window: the navigation rail, the title bar and the current page.
// The app creates it on demand and destroys it when it closes, so it keeps
// no state that matters beyond the page shown.
Window {
    id: win

    // Right-to-left languages (Arabic, Persian) mirror the whole window,
    // including its sheets and menus in the overlay.
    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true

    // home, history, device, devices, notifications, actions, general, about
    property string page: "home"
    property string deviceId
    // The app sets this when the OS draws a translucent backdrop behind us.
    property bool backdrop: false
    // And these when a native title bar lies over the content (macOS). Its
    // window buttons are over the sidebar unless nutty's language runs the
    // other way from the system's; the page's title bar then makes room.
    property int titleBarInset: 0
    property int windowButtonsWidth: 0
    property bool windowButtonsOnRight: false
    readonly property bool buttonsOverSidebar: windowButtonsOnRight === (Qt.application.layoutDirection === Qt.RightToLeft)

    // Pages seen before and after this one, as { page, id }, for Back and
    // Forward. Only a device page keeps its id: the others take one-off
    // requests ("add", ...) that shouldn't happen again on the way back.
    property var backStack: []
    property var forwardStack: []
    readonly property bool canGoBack: backStack.length > 0

    function here() {
        return { page: win.page, id: win.page === "device" ? win.deviceId : "" }
    }
    function isHere(entry) {
        const now = here()
        return entry.page === now.page && entry.id === now.id
    }
    function titleOf(entry) {
        if (entry.page === "device")
            return Devices.get(entry.id).name || pages.device.title
        return (pages[entry.page] || pages.home).title
    }

    function openPage(p, id) {
        const name = String(p || "home")
        const arg = id === undefined || id === null ? "" : String(id)
        if (name === "device" && arg.length === 0)
            return
        const target = pages[name] !== undefined ? name : "home"
        if (target !== win.page || (target === "device" && arg !== win.deviceId)) {
            backStack = backStack.concat([here()]).slice(-50)
            forwardStack = []
        }
        win.deviceId = arg
        win.page = target
    }

    // Steps through `from` to the nearest page that still exists and isn't
    // this one (a device may have been removed since), leaving this page on
    // `to`. Returns the two stacks.
    function step(from, to) {
        from = from.slice()
        while (from.length > 0) {
            const entry = from.pop()
            if (isHere(entry) || (entry.page === "device" && Devices.indexOf(entry.id) < 0))
                continue
            to = to.concat([here()])
            win.deviceId = entry.id
            win.page = entry.page
            break
        }
        return [from, to]
    }
    function goBack() {
        const stacks = step(backStack, forwardStack)
        backStack = stacks[0]
        forwardStack = stacks[1]
    }
    function goForward() {
        const stacks = step(forwardStack, backStack)
        forwardStack = stacks[0]
        backStack = stacks[1]
    }

    readonly property var pages: ({
        home: { title: qsTr("Home"), component: homePage },
        history: { title: qsTr("History"), component: historyPage },
        device: { title: qsTr("Device"), component: devicePage },
        devices: { title: qsTr("Devices"), component: devicesPage },
        notifications: { title: qsTr("Notifications"), component: notificationsPage },
        actions: { title: qsTr("Power actions"), component: actionsPage },
        general: { title: qsTr("Settings"), component: generalPage },
        about: { title: qsTr("About"), component: aboutPage },
    })
    readonly property var current: pages[page] !== undefined ? pages[page] : pages.home

    width: 1180
    height: 780
    minimumWidth: 720
    minimumHeight: 520
    title: page === "home" ? "nutty" : current.title + " – nutty"
    flags: Theme.frameless ? (Qt.Window | Qt.FramelessWindowHint) : Qt.Window
    color: Theme.frameless || backdrop ? "transparent" : Theme.window

    Shortcut {
        sequences: [StandardKey.Close]
        onActivated: win.close()
    }
    Shortcut {
        sequences: [StandardKey.Refresh]
        onActivated: Devices.refresh()
    }
    // True while a dialog or menu has the keyboard. Back and Forward wait
    // then: leaving the page would drop what's in it.
    readonly property bool dialogOpen: {
        const overlay = frame.Overlay.overlay
        for (let item = win.activeFocusItem; item; item = item.parent) {
            if (item === overlay)
                return true
        }
        return false
    }

    // Not StandardKey.Back: on Linux that includes Backspace, which would
    // leave the page whenever it's pressed outside a text field.
    Shortcut {
        sequences: [Qt.platform.os === "osx" ? "Ctrl+[" : "Alt+Left", "Back"]
        enabled: !win.dialogOpen
        onActivated: win.goBack()
    }
    Shortcut {
        sequences: [Qt.platform.os === "osx" ? "Ctrl+]" : "Alt+Right", "Forward"]
        enabled: !win.dialogOpen
        onActivated: win.goForward()
    }
    NavigationButtons {
        window: win
        enabled: !win.dialogOpen
        onBack: win.goBack()
        onForward: win.goForward()
    }

    WindowFrame {
        id: frame
        window: win
        color: win.backdrop ? "transparent" : Theme.window

        RowLayout {
            anchors.fill: parent
            spacing: 0

            Sidebar {
                id: sidebar
                Layout.fillHeight: true
                window: win
                compact: win.width < 900
                cornerRadius: Math.max(0, frame.radius - 1)
                topInset: win.buttonsOverSidebar ? win.titleBarInset : 0
                insetWidth: win.buttonsOverSidebar ? win.windowButtonsWidth : 0
                color: win.backdrop ? "transparent" : Theme.surface
                subtitle: qsTr("Power monitor")
                currentKey: win.page === "device" ? "devices" : win.page
                items: [
                    { key: "home", text: qsTr("Home"), icon: "home" },
                    { key: "history", text: qsTr("History"), icon: "history" },
                    { key: "devices", text: qsTr("Devices"), icon: "devices", badge: Summary.onBatteryCount > 0 ? Summary.onBatteryCount : undefined },
                    { key: "notifications", text: qsTr("Notifications"), icon: "bell" },
                    { key: "actions", text: qsTr("Power actions"), icon: "shield", badge: Actions.pending ? "!" : undefined },
                ]
                footerItems: [
                    { key: "general", text: qsTr("Settings"), icon: "gear" },
                    { key: "about", text: qsTr("About"), icon: "info" },
                ]
                onActivated: key => win.openPage(key, "")
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                // Over a native backdrop only the sidebar lets it through.
                color: win.backdrop ? Theme.window : "transparent"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.topMargin: win.buttonsOverSidebar ? 0 : win.titleBarInset
                    spacing: 0

                    TitleBar {
                        Layout.fillWidth: true
                        window: win
                        title: win.page === "device" ? Devices.get(win.deviceId).name || win.current.title : win.current.title
                        showBack: win.canGoBack
                        backToolTip: win.canGoBack ? qsTr("Back to %1").arg(win.titleOf(win.backStack[win.backStack.length - 1]))
                                                   : ""
                        onBack: win.goBack()

                        IconButton {
                            iconName: "refresh"
                            toolTip: qsTr("Refresh now")
                            onClicked: Devices.refresh(win.page === "device" ? win.deviceId : "")
                        }
                        IconButton {
                            iconName: Theme.dark ? "sun" : "moon"
                            toolTip: Theme.dark ? qsTr("Switch to light mode") : qsTr("Switch to dark mode")
                            onClicked: Preferences.theme = Theme.dark ? "Light" : "Dark"
                        }
                    }

                    Loader {
                        id: pageLoader
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        sourceComponent: win.current.component
                        onLoaded: {
                            if (!Theme.animationsEnabled)
                                return
                            item.opacity = 0
                            fadeIn.restart()
                        }
                        NumberAnimation {
                            id: fadeIn
                            target: pageLoader.item
                            property: "opacity"
                            to: 1
                            duration: Theme.durationNormal
                            easing.type: Easing.OutCubic
                        }
                        Connections {
                            target: pageLoader.item
                            ignoreUnknownSignals: true
                            function onNavigate(page, id) { win.openPage(page, id) }
                        }
                    }
                }
            }
        }
    }

    Component { id: homePage; HomePage {} }
    Component { id: historyPage; HistoryPage {} }
    Component { id: devicePage; DevicePage { deviceId: win.deviceId } }
    Component { id: devicesPage; DevicesPage { request: win.deviceId } }
    Component { id: notificationsPage; NotificationsPage { request: win.deviceId } }
    Component { id: actionsPage; ActionsPage { request: win.deviceId } }
    Component { id: generalPage; GeneralPage {} }
    Component { id: aboutPage; AboutPage {} }
}
