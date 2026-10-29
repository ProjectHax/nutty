// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Layouts
import Nutty

// Asks before a setting on a device changes. Every change gets a question;
// one that can cut power gets a second, saying what could happen.
// accepted() follows only a yes to each, declined() anything else.
//
// The second question's yes sits where the first one's "Cancel" was and
// wakes up after a moment, so a quick double click can't answer both.
Sheet {
    id: root

    // From DeviceControls.confirmation().
    property var question: ({})
    property int step: 0
    property bool answered: false

    signal accepted()
    signal declined()

    function ask(q) {
        question = q
        step = 0
        answered = false
        open()
    }

    readonly property bool warning: step === 1

    preferredWidth: 480
    iconName: warning ? "warning" : (question.icon || "sliders")
    iconColor: warning ? Theme.critical : Theme.accent
    title: (warning ? question.warningTitle : question.title) || ""
    subtitle: question.twice ? (warning ? qsTr("Question 2 of 2") : qsTr("Question 1 of 2")) : ""

    onClosed: {
        if (!answered)
            declined()
    }

    Text {
        Layout.fillWidth: true
        Layout.bottomMargin: 8
        text: (root.warning ? root.question.warningMessage : root.question.message) || ""
        color: root.warning ? Theme.text : Theme.subtext
        wrapMode: Text.Wrap
        lineHeight: 1.15
    }

    Timer {
        id: wake
        interval: 800
    }

    footerItems: [
        // Question 1: [Cancel] [Change]
        PillButton {
            objectName: "confirmFirst"
            visible: !root.warning
            text: root.question.confirmText || qsTr("Change")
            onClicked: {
                if (root.question.twice) {
                    root.step = 1
                    wake.restart()
                    return
                }
                root.answered = true
                root.accepted()
                root.close()
            }
        },
        PillButton {
            objectName: "confirmCancel"
            kind: "secondary"
            text: qsTr("Cancel")
            onClicked: root.close()
        },
        // Question 2: [Yes, change it] [Cancel]
        PillButton {
            objectName: "confirmSecond"
            visible: root.warning
            enabled: !wake.running
            tint: Theme.critical
            text: root.question.warningConfirmText || qsTr("Yes, change it")
            onClicked: {
                root.answered = true
                root.accepted()
                root.close()
            }
        }
    ]
}
