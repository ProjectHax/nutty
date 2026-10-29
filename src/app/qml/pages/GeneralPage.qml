// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Layouts
import Nutty

// App-wide settings: looks, startup, language, history and credentials.
ScrollPage {
    id: page

    SectionHeader { text: qsTr("Appearance") }
    Card {
        Layout.fillWidth: true
        padding: Theme.listPadding
        ColumnLayout {
            width: parent.width
            spacing: 0
            SettingRow {
                first: true
                iconName: Theme.dark ? "moon" : "sun"
                title: qsTr("Theme")
                Segmented {
                    options: [
                        { value: "System", text: qsTr("System") },
                        { value: "Light", text: qsTr("Light") },
                        { value: "Dark", text: qsTr("Dark") },
                    ]
                    value: Preferences.theme
                    onActivated: value => Preferences.theme = value
                }
            }
            SettingRow {
                iconName: "sliders"
                title: qsTr("Animations")
                Segmented {
                    options: [
                        { value: "System", text: qsTr("System") },
                        { value: "On", text: qsTr("On") },
                        { value: "Off", text: qsTr("Off") },
                    ]
                    value: Preferences.motion
                    onActivated: value => Preferences.motion = value
                }
            }
            SettingRow {
                visible: Preferences.translucencyOption
                iconName: "image"
                title: qsTr("Translucent window")
                Toggle {
                    checked: Preferences.translucentWindow
                    onToggled: Preferences.translucentWindow = checked
                }
            }
            SettingRow {
                visible: Preferences.titleBarOption
                iconName: "window"
                title: qsTr("Use the system title bar")
                Toggle {
                    checked: Preferences.useSystemTitleBar
                    onToggled: Preferences.useSystemTitleBar = checked
                }
            }
            SettingRow {
                iconName: Preferences.trayIcon === "Battery" ? "battery" : "acorn"
                title: qsTr("Tray icon")
                Segmented {
                    options: [
                        { value: "Acorn", text: qsTr("Acorn") },
                        { value: "Battery", text: qsTr("Battery") },
                    ]
                    value: Preferences.trayIcon
                    onActivated: value => Preferences.trayIcon = value
                }
            }
            SettingRow {
                iconName: "contrast"
                title: qsTr("Monochrome tray icon")
                Toggle {
                    checked: Preferences.monochromeTrayIcon
                    onToggled: Preferences.monochromeTrayIcon = checked
                }
            }
        }
    }

    SectionHeader { text: qsTr("Startup and memory") }
    Card {
        Layout.fillWidth: true
        padding: Theme.listPadding
        ColumnLayout {
            width: parent.width
            spacing: 0
            SettingRow {
                first: true
                iconName: "power"
                title: qsTr("Start at login")
                description: Preferences.autostartSupported
                             ? ""
                             : qsTr("Not supported here; add nutty to your startup apps with --hidden.")
                Toggle {
                    enabled: Preferences.autostartSupported
                    checked: Preferences.autostart
                    onToggled: Preferences.autostart = checked
                }
            }
            SettingRow {
                iconName: "gauge"
                title: qsTr("Keep the window loaded")
                description: qsTr("Opens faster, but uses more memory while nutty sits in the tray.")
                Toggle {
                    checked: Preferences.keepWindowLoaded
                    onToggled: Preferences.keepWindowLoaded = checked
                }
            }
        }
    }

    SectionHeader { text: qsTr("Language") }
    Card {
        Layout.fillWidth: true
        padding: Theme.listPadding
        SettingRow {
            width: parent.width
            first: true
            iconName: "globe"
            title: qsTr("Language")
            Dropdown {
                implicitWidth: 220
                options: Preferences.languages.map(l => ({ value: l.code, text: l.name }))
                value: Preferences.language
                onPicked: value => Preferences.language = value
            }
        }
    }

    SectionHeader { text: qsTr("History") }
    Card {
        Layout.fillWidth: true
        padding: Theme.listPadding
        ColumnLayout {
            width: parent.width
            spacing: 0
            SettingRow {
                first: true
                iconName: "history"
                title: qsTr("Keep events for")
                InputField {
                    implicitWidth: 120
                    text: String(Preferences.eventRetentionDays)
                    suffix: qsTr("days")
                    horizontalAlignment: TextInput.AlignRight
                    validator: IntValidator { bottom: 1; top: 3650 }
                    onEditingFinished: Preferences.eventRetentionDays = Number(text)
                }
            }
            SettingRow {
                iconName: "gauge"
                title: qsTr("Keep readings for")
                InputField {
                    implicitWidth: 120
                    text: String(Preferences.sampleRetentionDays)
                    suffix: qsTr("days")
                    horizontalAlignment: TextInput.AlignRight
                    validator: IntValidator { bottom: 1; top: 3650 }
                    onEditingFinished: Preferences.sampleRetentionDays = Number(text)
                }
            }
        }
    }

    SectionHeader { text: qsTr("Passwords and keys") }
    Card {
        Layout.fillWidth: true
        padding: Theme.listPadding
        ColumnLayout {
            width: parent.width
            spacing: 0
            SettingRow {
                first: true
                iconName: Preferences.secureStorage ? "lock" : "warning"
                title: Preferences.secureStorage ? qsTr("Stored in %1").arg(Preferences.secretBackend)
                                                 : qsTr("Stored in a file")
                description: Preferences.secureStorage ? "" : Preferences.secretWarning
                StatusChip {
                    status: Preferences.secureStorage ? "ok" : "warning"
                    text: Preferences.secureStorage ? qsTr("Secure") : qsTr("Not encrypted")
                }
            }
            SettingRow {
                iconName: "sliders"
                title: qsTr("Settings file")
                description: Preferences.configPath
                PillButton {
                    kind: "secondary"
                    iconName: "external"
                    text: qsTr("Show folder")
                    onClicked: AppInfo.openFolder(Preferences.configPath)
                }
            }
        }
    }

    Text {
        Layout.fillWidth: true
        visible: Preferences.lastError.length > 0
        text: Preferences.lastError
        color: Theme.critical
        wrapMode: Text.Wrap
    }
}
