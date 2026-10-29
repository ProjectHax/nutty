// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

class QQmlEngine;
class QQuickWindow;

namespace nutty {

// Picks the Qt Quick Controls style for this OS. Call before creating the
// first QML engine. QT_QUICK_CONTROLS_STYLE in the environment still wins.
//   Windows: FluentWinUI3 (Qt >= 6.8) or Universal
//   macOS:   macOS
//   Linux:   Fusion, driven by the palette ThemeBridge installs
void selectQuickControlsStyle();

// Global window setup that must happen before any QQuickWindow exists:
// an alpha buffer for the frameless, rounded Linux window.
void prepareQuickWindows();

// Everything an engine needs to load the Nutty module: the qrc import path
// (Qt < 6.5 lacks it), the image providers ("icons", "art"). For a
// QQmlApplicationEngine it also watches every root window's renderer.
void setupQmlEngine(QQmlEngine& engine);

// Turns Theme.effectsEnabled off if the window ends up on the software
// renderer (e.g. no usable GPU driver), where shader effects don't draw.
void watchRenderer(QQuickWindow* window);

}  // namespace nutty
