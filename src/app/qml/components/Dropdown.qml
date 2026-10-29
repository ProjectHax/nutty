// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import Nutty

// A pop-up list of choices in the app's style.
//   options: [{ value, text }]; `value` is the selected one, activated(value)
//   reports a pick.
T.ComboBox {
    id: control

    property var options: []
    property var value
    property string placeholder: qsTr("Choose…")

    signal picked(var value)

    // Shows `value` again after a pick that wasn't kept.
    function resync() {
        currentIndex = Qt.binding(function() { return control.valueIndex })
    }

    readonly property int valueIndex: {
        for (let i = 0; i < options.length; ++i) {
            if (options[i].value === value)
                return i
        }
        return -1
    }

    model: options
    textRole: "text"
    valueRole: "value"
    currentIndex: valueIndex
    implicitWidth: 240
    implicitHeight: Theme.controlHeight
    leftPadding: 12
    rightPadding: 34
    hoverEnabled: true
    font.pixelSize: Theme.fontSize

    onActivated: index => {
        if (index >= 0 && index < options.length)
            control.picked(options[index].value)
    }

    delegate: T.ItemDelegate {
        id: item
        required property var modelData
        required property int index
        width: ListView.view ? ListView.view.width : implicitWidth
        implicitHeight: 34
        leftPadding: 12
        rightPadding: 12
        highlighted: control.highlightedIndex === index
        hoverEnabled: true
        contentItem: Row {
            spacing: 8
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: "check"
                size: 14
                color: Theme.accent
                opacity: item.index === control.valueIndex ? 1 : 0
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: item.width - 46
                text: item.modelData.text
                color: Theme.text
                font.pixelSize: Theme.fontSize
                elide: Text.ElideRight
            }
        }
        background: Rectangle {
            radius: Theme.radiusSmall
            color: item.highlighted || item.hovered ? Theme.hover : "transparent"
        }
    }

    indicator: Icon {
        x: control.width - width - 12
        anchors.verticalCenter: parent.verticalCenter
        name: "chevron-down"
        size: 14
        color: Theme.subtext
    }

    contentItem: Text {
        text: control.valueIndex >= 0 ? control.options[control.valueIndex].text : control.placeholder
        color: control.valueIndex >= 0 ? (control.enabled ? Theme.text : Theme.faint) : Theme.faint
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: Theme.radiusSmall + 2
        color: control.down ? Theme.pressed : Theme.alpha(Theme.text, Theme.dark ? 0.06 : 0.04)
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? Theme.accent : control.hovered ? Theme.alpha(Theme.text, 0.28) : Theme.border
    }

    popup: T.Popup {
        y: control.height + 4
        width: Math.max(control.width, 200)
        implicitHeight: Math.min(contentItem.implicitHeight + 12, 320)
        padding: 6

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            ScrollIndicator.vertical: ScrollIndicator {}
        }

        background: Rectangle {
            radius: Theme.radius
            color: Theme.elevated
            border.width: 1
            border.color: Theme.border
            SoftShadow {
                z: -1
                anchors.fill: parent
                radius: Theme.radius
                blur: 12
                offsetY: 4
                strength: Theme.dark ? 0.7 : 0.5
            }
        }

        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast }
        }
    }
}
