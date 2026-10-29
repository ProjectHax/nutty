// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Shapes
import Nutty

// A battery whose liquid fill follows `level` with a gentle wave.
// Green above 50 %, amber to 20 %, red below. While charging a bolt pulses;
// offline batteries get a dashed outline and no fill.
//
// The wave only animates while the window is active, so an idle nutty
// window doesn't keep the GPU busy.
Item {
    id: root

    property real level: -1          // percent; < 0 = unknown
    property bool charging: false
    property bool offline: false
    property bool stale: false
    property bool vertical: false
    property bool showLabel: true
    property bool showTip: true
    property bool animate: true

    readonly property bool known: level >= 0 && !offline
    readonly property real fraction: known ? Math.max(0, Math.min(1, level / 100)) : 0
    property real shownFraction: fraction
    readonly property color fillColor: stale ? Theme.mix(Theme.levelColor(level), Theme.faint, 0.6) : Theme.levelColor(level)
    readonly property real longSide: vertical ? height : width
    readonly property real shortSide: vertical ? width : height
    readonly property real capLength: showTip ? Math.max(2, longSide * 0.06) : 0
    readonly property real capWidth: shortSide * 0.4
    readonly property real stroke: Math.max(1.5, shortSide * 0.055)
    readonly property real gap: Math.max(1.5, stroke * 0.9)
    readonly property real bodyRadius: Math.min(shortSide * 0.2, Theme.radius)
    readonly property real innerRadius: Math.max(1, bodyRadius - stroke - gap * 0.6)
    readonly property color frameColor: Theme.alpha(Theme.text, offline ? 0.35 : (Theme.dark ? 0.42 : 0.32))
    readonly property bool waving: animate && Theme.animationsEnabled && known && visible && Window.active

    implicitWidth: vertical ? 56 : 112
    implicitHeight: vertical ? 100 : 54

    Behavior on shownFraction {
        enabled: Theme.animationsEnabled
        NumberAnimation { duration: 900; easing.type: Easing.OutCubic }
    }

    Accessible.role: Accessible.Indicator
    Accessible.name: known ? qsTr("Battery %1 %").arg(Math.round(level)) : qsTr("Battery level unknown")

    // The terminal nub.
    Rectangle {
        visible: root.showTip
        color: root.frameColor
        radius: Math.min(width, height) * 0.35
        x: root.vertical ? (root.width - width) / 2 : root.width - width
        y: 0
        width: root.vertical ? root.capWidth : root.capLength + root.stroke
        height: root.vertical ? root.capLength + root.stroke : root.capWidth
        anchors.verticalCenter: root.vertical ? undefined : root.verticalCenter
    }

    Item {
        id: body
        x: 0
        y: root.vertical ? root.capLength : 0
        width: root.vertical ? root.width : root.width - root.capLength
        height: root.vertical ? root.height - root.capLength : root.height

        // Glass body.
        Rectangle {
            anchors.fill: parent
            radius: root.bodyRadius
            color: Theme.alpha(Theme.text, Theme.dark ? 0.07 : 0.045)
            border.width: root.offline ? 0 : root.stroke
            border.color: root.frameColor
        }

        Shape {
            anchors.fill: parent
            visible: root.offline
            RoundedRectPath {
                w: body.width
                h: body.height
                r: root.bodyRadius
                strokeWidth: root.stroke
                strokeColor: root.frameColor
                strokeStyle: ShapePath.DashLine
                dashPattern: [2.2, 1.6]
                fillColor: "transparent"
            }
        }

        Item {
            id: inner
            anchors.fill: parent
            anchors.margins: root.stroke + root.gap

            // Liquid (shader).
            Loader {
                anchors.fill: parent
                active: Theme.effectsEnabled && root.known
                sourceComponent: Item {
                    ShaderEffect {
                        id: liquid
                        width: root.vertical ? parent.width : parent.height
                        height: root.vertical ? parent.height : parent.width
                        x: (parent.width - width) / 2
                        y: (parent.height - height) / 2
                        rotation: root.vertical ? 0 : 90

                        property color colorTop: Qt.lighter(root.fillColor, 1.06)
                        property color colorBottom: Qt.darker(root.fillColor, 1.04)
                        property size itemSize: Qt.size(width, height)
                        property real level: root.shownFraction
                        property real phase: 0
                        property real amplitude: Math.max(0.8, width * (root.vertical ? 0.035 : 0.06))
                        property real radius: root.innerRadius

                        NumberAnimation on phase {
                            from: 0
                            to: Math.PI * 2
                            duration: root.charging ? 2600 : 4200
                            loops: Animation.Infinite
                            running: root.waving
                        }

                        fragmentShader: "qrc:/nutty/shaders/liquid.frag.qsb"
                    }
                }
            }

            // Liquid (fallback): a plain bar.
            Rectangle {
                visible: !Theme.effectsEnabled && root.known
                radius: root.innerRadius
                x: 0
                y: root.vertical ? parent.height - height : 0
                width: root.vertical ? parent.width : Math.max(radius * 2 * (root.shownFraction > 0 ? 1 : 0), parent.width * root.shownFraction)
                height: root.vertical ? Math.max(radius * 2 * (root.shownFraction > 0 ? 1 : 0), parent.height * root.shownFraction) : parent.height
                color: root.fillColor
            }
        }

        Grid {
            id: content
            anchors.centerIn: parent
            columns: root.vertical ? 1 : 2
            spacing: 2
            horizontalItemAlignment: Grid.AlignHCenter
            verticalItemAlignment: Grid.AlignVCenter

            Icon {
                id: bolt
                visible: root.charging && root.known
                name: "bolt"
                size: Math.round(Math.min(body.width, body.height) * (root.showLabel ? 0.36 : 0.5))
                color: "#ffffff"
                SequentialAnimation on scale {
                    running: root.waving && root.charging
                    loops: Animation.Infinite
                    NumberAnimation { from: 1.0; to: 1.12; duration: 1100; easing.type: Easing.InOutSine }
                    NumberAnimation { from: 1.12; to: 1.0; duration: 1100; easing.type: Easing.InOutSine }
                }
            }

            Text {
                id: label
                visible: root.showLabel
                text: root.known ? Math.round(root.level) + "%" : "—"
                // Over the liquid the label is white; over the empty glass it uses the text colour.
                color: root.known && root.shownFraction > 0.52 ? "#ffffff" : Theme.text
                font.pixelSize: Math.max(9, Math.round(Math.min(body.height * 0.42, body.width * 0.3)))
                font.weight: Font.Bold
                style: root.known && root.shownFraction > 0.52 ? Text.Raised : Text.Normal
                styleColor: Qt.rgba(0, 0, 0, 0.18)
            }
        }

        Icon {
            visible: root.offline
            anchors.centerIn: parent
            name: "plug-off"
            size: Math.round(Math.min(body.width, body.height) * 0.5)
            color: root.frameColor
        }
    }
}
