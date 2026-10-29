// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Layouts
import Nutty

// Power rules (what to do when a battery runs low), device groups, the
// timing that keeps actions safe, and the snooze and dry-run switches.
ScrollPage {
    id: page

    // "add" or "add:<device or group id>" opens a new rule right away.
    property string request

    // Rules by what they watch: each device, then each group, then rules
    // whose device or group is gone.
    readonly property var sections: {
        const rules = Catalog.rules
        const out = []
        const placed = {}
        for (const d of Catalog.devices) {
            const list = rules.filter(r => r.scope === "Device" && r.target === d.id)
            if (list.length > 0)
                out.push({ title: d.name, detail: d.typeName, icon: d.icon, rules: list })
        }
        for (const g of Catalog.groups) {
            const list = rules.filter(r => r.scope !== "Device" && r.target === g.id)
            if (list.length > 0)
                out.push({ title: g.name, detail: qsTr("Group"), icon: "devices", rules: list })
        }
        for (const s of out) {
            for (const r of s.rules)
                placed[r.id] = true
        }
        const rest = rules.filter(r => placed[r.id] !== true)
        if (rest.length > 0)
            out.push({ title: qsTr("Missing device or group"), detail: "", icon: "warning", rules: rest })
        return out
    }

    function actionIcon(action) {
        switch (action) {
        case "Notify": return "bell"
        case "Command": return "terminal"
        case "Sleep": return "moon"
        case "Hibernate": return "pause"
        default: return "power"
        }
    }

    function addRule(target) {
        ruleEditor.reset(target || "")
        ruleSheet.open()
    }

    function handleRequest() {
        if (request === "add")
            addRule("")
        else if (request.startsWith("add:"))
            addRule(request.substring(4))
    }

    Component.onCompleted: handleRequest()
    onRequestChanged: handleRequest()

    // What's going on right now.
    Card {
        Layout.fillWidth: true
        visible: Actions.pending
        color: Theme.alpha(Actions.fsd ? Theme.critical : Theme.warning, Theme.dark ? 0.16 : 0.1)
        RowLayout {
            width: parent.width
            spacing: 14
            Icon {
                name: "timer"
                size: 26
                color: Actions.fsd ? Theme.critical : Theme.warning
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    Layout.fillWidth: true
                    text: Actions.title
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeLarge
                    font.weight: Font.DemiBold
                    wrapMode: Text.Wrap
                }
                Text {
                    Layout.fillWidth: true
                    text: Actions.reason + (Actions.ruleName.length > 0 ? " · " + Actions.ruleName : "")
                    color: Theme.subtext
                    wrapMode: Text.Wrap
                }
            }
            PillButton {
                visible: Actions.cancellable
                tint: Theme.critical
                iconName: "close"
                text: qsTr("Cancel")
                onClicked: Actions.cancel()
            }
        }
    }

    SectionHeader {
        text: qsTr("Power rules")
        detail: qsTr("%n rule(s)", "", Catalog.rules.length)
        PillButton {
            iconName: "plus"
            text: qsTr("Add rule")
            enabled: Catalog.devices.length > 0
            onClicked: page.addRule("")
        }
    }

    Card {
        Layout.fillWidth: true
        visible: Catalog.rules.length === 0
        EmptyState {
            width: parent.width
            iconName: "power"
            title: qsTr("No power rules")
            text: Catalog.devices.length === 0 ? qsTr("Add a device first.") : ""
            actionText: Catalog.devices.length === 0 ? qsTr("Add device") : qsTr("Add rule")
            onActionClicked: Catalog.devices.length === 0 ? page.navigate("devices", "add") : page.addRule("")
        }
    }

    Repeater {
        model: page.sections
        delegate: ColumnLayout {
            id: section
            required property var modelData
            Layout.fillWidth: true
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Icon {
                    name: section.modelData.icon
                    size: 15
                    color: Theme.subtext
                }
                Text {
                    text: section.modelData.title
                    color: Theme.text
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: section.modelData.detail
                    color: Theme.faint
                    font.pixelSize: Theme.fontSizeSmall
                    elide: Text.ElideRight
                }
            }

            Card {
                Layout.fillWidth: true
                padding: Theme.listPadding
                ColumnLayout {
                    width: parent.width
                    spacing: 0
                    Repeater {
                        model: section.modelData.rules
                        delegate: ListRow {
                            id: row
                            required property var modelData
                            required property int index
                            readonly property bool usable: modelData.available !== false
                            divider: index > 0
                            iconName: page.actionIcon(modelData.action)
                            iconColor: modelData.enabled ? Theme.subtext : Theme.faint
                            title: modelData.name
                            titleColor: modelData.enabled ? Theme.text : Theme.faint
                            description: qsTr("%1 → %2").arg(modelData.conditionText).arg(modelData.description)
                            elideTitle: true
                            details: RowLayout {
                                Layout.fillWidth: true
                                visible: !row.usable || row.modelData.caveat.length > 0
                                spacing: 6
                                Icon {
                                    name: "warning"
                                    size: 13
                                    color: row.usable ? Theme.warning : Theme.critical
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: row.usable ? row.modelData.caveat : qsTr("Not available here: %1").arg(row.modelData.caveat)
                                    color: row.usable ? Theme.warning : Theme.critical
                                    font.pixelSize: Theme.fontSizeSmall
                                    wrapMode: Text.Wrap
                                }
                            }

                            IconButton {
                                visible: row.modelData.action !== "Notify"
                                iconName: "play"
                                toolTip: qsTr("Simulate: run the countdown without doing anything")
                                enabled: !Actions.pending
                                onClicked: Actions.simulate(row.modelData.id)
                            }
                            Toggle {
                                checked: row.modelData.enabled
                                onToggled: Catalog.setRuleEnabled(row.modelData.id, checked)
                            }
                            IconButton {
                                iconName: "edit"
                                toolTip: qsTr("Edit")
                                onClicked: {
                                    ruleEditor.load(row.modelData.id)
                                    ruleSheet.open()
                                }
                            }
                            IconButton {
                                iconName: "trash"
                                danger: true
                                toolTip: qsTr("Remove")
                                onClicked: {
                                    confirmRemove.kind = "rule"
                                    confirmRemove.itemId = row.modelData.id
                                    confirmRemove.title = qsTr("Remove %1?").arg(row.modelData.name)
                                    confirmRemove.message = qsTr("nutty won't act on this rule any more.")
                                    confirmRemove.open()
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Snooze and dry run.
    SectionHeader { text: qsTr("While you're working on it") }
    Card {
        Layout.fillWidth: true
        padding: Theme.listPadding
        ColumnLayout {
            width: parent.width
            spacing: 0
            SettingRow {
                first: true
                iconName: "clock"
                title: Actions.snoozed ? qsTr("Snoozed until %1").arg(Actions.snoozedUntil.toLocaleTimeString(Qt.locale(), Locale.ShortFormat))
                                       : qsTr("Snooze power actions")
                PillButton {
                    kind: "secondary"
                    iconName: Actions.snoozed ? "play" : "pause"
                    text: Actions.snoozed ? qsTr("Resume") : qsTr("Snooze 1 hour")
                    onClicked: Actions.snoozed ? Actions.resume() : Actions.snooze(60)
                }
            }
            SettingRow {
                iconName: "shield"
                title: qsTr("Dry run")
                description: Actions.dryRunForced
                             ? qsTr("nutty was started with --dry-run: actions are only simulated.")
                             : qsTr("Simulate power actions instead of running them.")
                Toggle {
                    checked: Actions.dryRun
                    enabled: !Actions.dryRunForced
                    onToggled: Actions.dryRun = checked
                }
            }
            SettingRow {
                id: permissionRow
                // "" = not asked yet, "ok", or the error.
                property string outcome
                visible: Actions.shutdownNeedsPermission
                iconName: "power"
                title: qsTr("Permission to shut down")
                description: outcome === "ok"
                             ? qsTr("Granted. nutty can shut this Mac down.")
                             : outcome.length > 0
                               ? qsTr("Not granted: %1. Allow nutty under System Settings → Privacy & Security → Automation.").arg(outcome)
                               : qsTr("macOS asks once. Answer now, not during an outage.")
                PillButton {
                    kind: "secondary"
                    text: qsTr("Ask now")
                    onClicked: {
                        const error = Actions.requestShutdownPermission()
                        permissionRow.outcome = error.length > 0 ? error : "ok"
                    }
                }
            }
        }
    }

    // Groups of devices.
    SectionHeader {
        text: qsTr("Groups")
        detail: qsTr("%n group(s)", "", Catalog.groups.length)
        PillButton {
            kind: "secondary"
            iconName: "plus"
            text: qsTr("Add group")
            enabled: Catalog.devices.length > 0
            onClicked: groupSheet.edit(null)
        }
    }
    Card {
        Layout.fillWidth: true
        visible: Catalog.groups.length > 0
        padding: Theme.listPadding
        ColumnLayout {
            width: parent.width
            spacing: 0
            Repeater {
                model: Catalog.groups
                delegate: ListRow {
                    id: groupRow
                    required property var modelData
                    required property int index
                    divider: index > 0
                    iconName: "devices"
                    title: modelData.name
                    description: modelData.members.length === 0 ? qsTr("No devices") : modelData.memberNames
                    elideTitle: true
                    elideDescription: true

                    PillButton {
                        kind: "ghost"
                        iconName: "plus"
                        text: qsTr("Rule")
                        onClicked: {
                            ruleEditor.reset(groupRow.modelData.id)
                            ruleSheet.open()
                        }
                    }
                    IconButton {
                        iconName: "edit"
                        toolTip: qsTr("Edit")
                        onClicked: groupSheet.edit(groupRow.modelData)
                    }
                    IconButton {
                        iconName: "trash"
                        danger: true
                        toolTip: qsTr("Remove")
                        onClicked: {
                            confirmRemove.kind = "group"
                            confirmRemove.itemId = groupRow.modelData.id
                            confirmRemove.title = qsTr("Remove %1?").arg(groupRow.modelData.name)
                            confirmRemove.message = qsTr("Rules about this group are removed too.")
                            confirmRemove.open()
                        }
                    }
                }
            }
        }
    }

    // Timing.
    SectionHeader { text: qsTr("Timing") }
    Card {
        Layout.fillWidth: true
        padding: Theme.listPadding
        ColumnLayout {
            width: parent.width
            spacing: 0
            Repeater {
                model: [
                    { key: "safetyMarginSec", icon: "battery-low", title: qsTr("Safety margin"),
                      description: qsTr("A countdown ends early so this much runtime is left when the action runs.") },
                    { key: "stableOnlineSec", icon: "plug", title: qsTr("Power is back after") },
                    { key: "deadTimeSec", icon: "link-off", title: qsTr("Connection lost after") },
                    { key: "wakeGraceSec", icon: "moon", title: qsTr("Grace after waking"),
                      description: qsTr("After this computer wakes up, wait this long for fresh readings before acting.") },
                    { key: "fsdDelaySec", icon: "bolt", title: qsTr("Forced shutdown delay"),
                      description: qsTr("When the NUT server orders a shutdown, act after this many seconds. It can't be cancelled.") },
                ]
                delegate: SettingRow {
                    required property var modelData
                    required property int index
                    first: index === 0
                    iconName: modelData.icon
                    title: modelData.title
                    description: modelData.description || ""
                    InputField {
                        implicitWidth: 110
                        text: String(Preferences[modelData.key])
                        suffix: "s"
                        horizontalAlignment: TextInput.AlignRight
                        validator: IntValidator { bottom: 0; top: 3600 }
                        onEditingFinished: Preferences[modelData.key] = Number(text)
                    }
                }
            }
        }
    }

    Text {
        Layout.fillWidth: true
        visible: Preferences.lastError.length > 0 || Catalog.lastError.length > 0
        text: Preferences.lastError.length > 0 ? Preferences.lastError : Catalog.lastError
        color: Theme.critical
        wrapMode: Text.Wrap
    }

    RuleEditor { id: ruleEditor }

    RuleSheet {
        id: ruleSheet
        editor: ruleEditor
    }

    GroupSheet { id: groupSheet }

    ConfirmSheet {
        id: confirmRemove
        property string kind
        property string itemId
        onAccepted: kind === "group" ? Catalog.removeGroup(itemId) : Catalog.removeRule(itemId)
    }
}
