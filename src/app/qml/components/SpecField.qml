// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Layouts
import Nutty

// One field of a DeviceEditor/ChannelEditor form, drawn from its spec (see
// FieldSpecs.h): text, number, secret, bool, choice or multiline.
FormField {
    id: root

    property var editor
    property var spec: ({})
    readonly property string key: spec.key || ""
    readonly property string kind: spec.kind || "text"
    readonly property var current: editor && editor.values[key] !== undefined ? editor.values[key] : undefined
    readonly property string currentText: current === undefined || current === null ? "" : String(current)

    signal discoverRequested()

    label: kind === "bool" ? "" : (spec.label || "") + (spec.required ? " *" : "")
    help: spec.help || ""
    visible: editor ? editor.fieldVisible(spec) && (editor.values, true) : true

    Loader {
        sourceComponent: root.kind === "bool" ? boolField
                       : root.kind === "choice" ? choiceField
                       : root.kind === "multiline" ? multiField
                       : textField
    }

    Component {
        id: textField
        RowLayout {
            spacing: 8
            InputField {
                Layout.fillWidth: true
                secret: root.kind === "secret"
                placeholderText: root.spec.placeholder || ""
                inputMethodHints: root.kind === "number" ? Qt.ImhDigitsOnly : Qt.ImhNone
                validator: root.kind === "number" ? numberValidator : null
                text: root.currentText
                onTextEdited: root.editor.setValue(root.key, text)
                IntValidator {
                    id: numberValidator
                    bottom: root.spec.min !== undefined ? root.spec.min : 0
                    top: root.spec.max !== undefined ? root.spec.max : 999999
                }
            }
            PillButton {
                visible: root.spec.discover === true
                kind: "secondary"
                iconName: "search"
                text: qsTr("Find devices")
                onClicked: root.discoverRequested()
            }
        }
    }

    Component {
        id: boolField
        Toggle {
            text: root.spec.label || ""
            checked: root.current === true || root.current === "true"
            onToggled: root.editor.setValue(root.key, checked)
        }
    }

    Component {
        id: choiceField
        Dropdown {
            options: root.spec.options || []
            value: root.currentText
            onPicked: v => root.editor.setValue(root.key, v)
        }
    }

    Component {
        id: multiField
        Rectangle {
            implicitHeight: 92
            radius: Theme.radiusSmall + 2
            color: Theme.alpha(Theme.text, Theme.dark ? 0.06 : 0.04)
            border.width: area.activeFocus ? 2 : 1
            border.color: area.activeFocus ? Theme.accent : Theme.border
            TextEdit {
                id: area
                anchors.fill: parent
                anchors.margins: 10
                text: root.currentText
                color: Theme.text
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                selectionColor: Theme.alpha(Theme.accent, 0.35)
                onTextChanged: if (activeFocus) root.editor.setValue(root.key, text)
                Text {
                    anchors.fill: parent
                    visible: area.length === 0
                    text: root.spec.placeholder || ""
                    color: Theme.faint
                    font: area.font
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
