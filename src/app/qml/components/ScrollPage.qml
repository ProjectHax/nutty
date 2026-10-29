// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Nutty

// A scrolling page body: a column of sections with the app's margins,
// centred and capped at `maxContentWidth` on wide windows.
Flickable {
    id: flick

    default property alias body: column.data
    property real maxContentWidth: 1180
    property real margin: width < 640 ? 16 : 28
    readonly property real contentWidthUsed: column.width

    signal navigate(string page, string id)

    clip: true
    contentWidth: width
    contentHeight: column.implicitHeight + 48
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {}

    ColumnLayout {
        id: column
        width: Math.min(flick.width - flick.margin * 2, flick.maxContentWidth)
        x: (flick.width - width) / 2
        y: 12
        spacing: 18
    }
}
