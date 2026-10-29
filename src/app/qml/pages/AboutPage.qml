// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Layouts
import Nutty

// Who made this, which version it is, and the license.
ScrollPage {
    id: page

    Card {
        Layout.fillWidth: true
        padding: 28
        ColumnLayout {
            width: parent.width
            spacing: 10

            Image {
                Layout.alignment: Qt.AlignHCenter
                source: AppInfo.icon
                sourceSize.width: 112
                sourceSize.height: 112
                width: 112
                height: 112
                smooth: true
                mipmap: true
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: AppInfo.name
                color: Theme.text
                font.pixelSize: Theme.fontSizeDisplay
                font.weight: Font.Bold
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Version %1").arg(AppInfo.version)
                color: Theme.subtext
            }
            Text {
                Layout.fillWidth: true
                Layout.maximumWidth: 520
                Layout.alignment: Qt.AlignHCenter
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Keeps an eye on your UPSes and batteries from the system tray, tells you when the power goes out, and shuts this computer down safely before the battery runs dry.")
                color: Theme.text
                wrapMode: Text.Wrap
                lineHeight: 1.2
            }
            Flow {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 8
                spacing: 10
                PillButton {
                    kind: "secondary"
                    iconName: "globe"
                    text: qsTr("Website")
                    onClicked: AppInfo.openUrl(AppInfo.homepage)
                }
                PillButton {
                    kind: "secondary"
                    iconName: "message"
                    text: qsTr("Report a problem")
                    onClicked: AppInfo.openUrl(AppInfo.issues)
                }
                PillButton {
                    kind: "secondary"
                    iconName: "shield"
                    text: qsTr("License")
                    onClicked: AppInfo.openUrl(AppInfo.licenseUrl)
                }
            }
        }
    }

    Card {
        Layout.fillWidth: true
        padding: Theme.listPadding
        ColumnLayout {
            width: parent.width
            spacing: 0
            SettingRow {
                first: true
                iconName: "shield"
                title: AppInfo.copyright
                description: qsTr("nutty is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License, version 3 only (%1). It comes with ABSOLUTELY NO WARRANTY.").arg(AppInfo.license)
            }
            SettingRow {
                iconName: "info"
                title: qsTr("Built with Qt %1").arg(AppInfo.qtVersion)
                description: AppInfo.platform
            }
            SettingRow {
                iconName: "history"
                title: qsTr("History and pictures")
                description: AppInfo.dataDir
                PillButton {
                    kind: "secondary"
                    iconName: "external"
                    text: qsTr("Show folder")
                    onClicked: AppInfo.openFolder(AppInfo.dataDir)
                }
            }
        }
    }
}
