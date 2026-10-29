// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import QtQuick.Shapes
import Nutty

// A round gauge: a track over `sweepDegrees` (centred on 12 o'clock) and an
// animated progress arc with a gradient along it, value in the middle.
//
//   ArcGauge { value: 63; caption: qsTr("charge") }
//
// `value` runs from 0 to `maximum`; a negative value means unknown.
Item {
    id: root

    property real value: -1
    property real maximum: 100
    property real sweepDegrees: 270
    property real thickness: Math.max(5, Math.min(width, height) * 0.085)
    property color colorEnd: Theme.levelColor(maximum > 0 ? value / maximum * 100 : value)
    property color colorStart: Theme.mix(colorEnd, Theme.dark ? "#ffffff" : "#000000", 0.18)
    property color trackColor: Theme.alpha(Theme.text, Theme.dark ? 0.10 : 0.075)
    property string valueText: ""        // overrides the number
    property string unit: "%"
    property string caption: ""
    property bool showValue: true
    property int animationDuration: 800
    property int easingType: Easing.OutCubic

    readonly property bool known: value >= 0
    property real shown: known ? Math.min(1, value / Math.max(maximum, 0.0001)) : 0

    Behavior on shown {
        enabled: Theme.animationsEnabled
        NumberAnimation { duration: root.animationDuration; easing.type: root.easingType }
    }

    implicitWidth: 120
    implicitHeight: 120
    Accessible.role: Accessible.ProgressBar
    Accessible.name: caption
    Accessible.description: known ? Math.round(value) + unit : qsTr("unknown")

    Loader {
        anchors.fill: parent
        active: Theme.effectsEnabled
        sourceComponent: ShaderEffect {
            property color trackColor: root.trackColor
            property color colorStart: root.colorStart
            property color colorEnd: root.colorEnd
            property size itemSize: Qt.size(width, height)
            property real thickness: root.thickness
            property real progress: root.shown
            property real sweep: root.sweepDegrees * Math.PI / 180
            property real startAngle: root.sweepDegrees >= 360 ? 0 : -root.sweepDegrees / 2 * Math.PI / 180
            fragmentShader: "qrc:/nutty/shaders/ring.frag.qsb"
        }
    }

    // Fallback for the software renderer: two stroked arcs.
    Shape {
        id: fallback
        anchors.fill: parent
        visible: !Theme.effectsEnabled
        readonly property real r: Math.min(width, height) / 2 - root.thickness / 2 - 1
        readonly property real start: root.sweepDegrees >= 360 ? -90 : 90 + (360 - root.sweepDegrees) / 2

        ShapePath {
            strokeColor: root.trackColor
            strokeWidth: root.thickness
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                centerX: root.width / 2
                centerY: root.height / 2
                radiusX: fallback.r
                radiusY: fallback.r
                startAngle: fallback.start
                sweepAngle: root.sweepDegrees
            }
        }
        ShapePath {
            strokeColor: root.shown > 0 ? root.colorEnd : "transparent"
            strokeWidth: root.thickness
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                centerX: root.width / 2
                centerY: root.height / 2
                radiusX: fallback.r
                radiusY: fallback.r
                startAngle: fallback.start
                sweepAngle: Math.max(0.01, root.sweepDegrees * root.shown)
            }
        }
    }

    Column {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: root.sweepDegrees < 360 ? root.height * 0.03 : 0
        visible: root.showValue
        spacing: -2

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            AnimatedNumber {
                visible: root.valueText.length === 0
                value: root.known ? root.value : -1
                color: Theme.text
                font.pixelSize: Math.round(root.height * 0.24)
                font.weight: Font.Bold
            }
            Text {
                visible: root.valueText.length > 0
                text: root.valueText
                color: Theme.text
                font.pixelSize: Math.round(root.height * 0.24)
                font.weight: Font.Bold
            }
            Text {
                visible: root.known && root.unit.length > 0
                text: root.unit
                color: Theme.subtext
                font.pixelSize: Math.round(root.height * 0.11)
                font.weight: Font.DemiBold
                anchors.baseline: parent.children[0].visible ? parent.children[0].baseline : parent.children[1].baseline
                leftPadding: 2
            }
        }
        Text {
            visible: root.caption.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.caption
            color: Theme.subtext
            font.pixelSize: Math.max(10, Math.round(root.height * 0.095))
        }
    }
}
