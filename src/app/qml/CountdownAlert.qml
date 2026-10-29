// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import Nutty

// The countdown alert, bound to the pending power action. The app shows it
// when an action is scheduled and closes it when the action is gone.
CountdownWindow {
    id: win

    action: Actions.action.length > 0 ? Actions.action : "shutdown"
    actionText: Actions.simulation ? simulatedTitle(Actions.action) : ""
    deviceName: Actions.targetName.length > 0 ? Actions.targetName : Actions.deviceName
    // A simulation's reason is just "Simulation"; the rule says more.
    reason: Actions.simulation && Actions.ruleName.length > 0 ? Actions.ruleName : Actions.reason
    remaining: Actions.remaining
    total: Actions.total
    cancellable: Actions.cancellable
    // "Do it now" would skip the countdown's safety; it is not offered.
    canRunNow: false
    note: Actions.simulation ? qsTr("Nothing will happen: this is a dry run.") : ""

    function simulatedTitle(a) {
        switch (a) {
        case "hibernate": return qsTr("Simulated hibernation")
        case "sleep": return qsTr("Simulated sleep")
        case "command": return qsTr("Simulated command")
        case "notify": return qsTr("Simulated notification")
        }
        return qsTr("Simulated shutdown")
    }

    onCancelRequested: {
        Actions.cancel()
        win.close()
    }
}
