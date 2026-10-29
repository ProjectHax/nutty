// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import Nutty

// A number that glides to its new value. Negative values mean "unknown".
Text {
    id: root

    property real value: -1
    property int decimals: 0
    property string prefix: ""
    property string suffix: ""
    readonly property bool unknown: value < 0
    property real shown: unknown ? 0 : value

    Behavior on shown {
        enabled: Theme.animationsEnabled
        NumberAnimation { duration: Theme.durationSlow; easing.type: Easing.OutCubic }
    }

    text: unknown ? "—" : prefix + Number(shown).toLocaleString(Qt.locale(), "f", decimals) + suffix
    color: Theme.text
}
