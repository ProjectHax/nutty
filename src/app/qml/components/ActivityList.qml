// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import Nutty

// The activity timeline, grouped by day. Works with any list model whose
// rows have the roles
//   timestamp, type, severity, deviceName, title, message, day
// where `day` is a section label such as "Today" (rows of one day must be
// adjacent). With `interactive: false` the list sizes itself to its rows,
// for embedding in a page that scrolls on its own; pass a model that is
// already trimmed to the rows you want to show.
ListView {
    id: root

    property bool showDevice: true
    property string emptyText: qsTr("Nothing has happened yet.")
    readonly property bool empty: count === 0

    implicitWidth: 420
    implicitHeight: interactive ? 320 : Math.max(empty ? emptyState.implicitHeight : 0, contentHeight)
    clip: interactive
    spacing: 0
    boundsBehavior: Flickable.StopAtBounds
    reuseItems: true

    section.property: "day"
    section.criteria: ViewSection.FullString
    section.delegate: Item {
        required property string section
        width: ListView.view.width
        height: section.length > 0 ? 34 : 0
        visible: section.length > 0
        Text {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 8
            text: parent.section
            color: Theme.subtext
            font.pixelSize: Theme.fontSizeSmall
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 0.6
        }
    }

    delegate: EventRow {
        width: ListView.view.width
        timestamp: model.timestamp
        type: model.type
        severity: model.severity
        deviceName: model.deviceName ?? ""
        title: model.title
        message: model.message ?? ""
        showDevice: root.showDevice
        firstInGroup: ListView.previousSection !== ListView.section || index === 0
        lastInGroup: ListView.nextSection !== ListView.section || index === root.count - 1
    }

    Column {
        id: emptyState
        visible: root.empty
        y: root.interactive ? Math.max(0, (root.height - height) / 2) : 0
        width: root.width
        topPadding: 18
        bottomPadding: 18
        spacing: 10
        Icon {
            anchors.horizontalCenter: parent.horizontalCenter
            name: "history"
            size: 28
            color: Theme.faint
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: root.emptyText
            color: Theme.subtext
            wrapMode: Text.Wrap
        }
    }
}
