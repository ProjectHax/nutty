// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "app/shell/Application.h"
#include "app/ui/UiSetup.h"

#include <QApplication>
#include <QIcon>
#include <QtQml/QQmlExtensionPlugin>

Q_IMPORT_QML_PLUGIN(NuttyPlugin)

int main(int argc, char* argv[])
{
    // Screenshots never open a window on the desktop.
    if (nutty::wantsScreenshot(argc, argv) && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication::setApplicationName(QStringLiteral("nutty"));
    QApplication::setApplicationDisplayName(QStringLiteral("nutty"));
    QApplication::setOrganizationName(QStringLiteral("ProjectHax"));
    QApplication::setApplicationVersion(QStringLiteral(NUTTY_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral(NUTTY_APP_ID));
    nutty::prepareQuickWindows();

    QApplication app(argc, argv);
    // The tray decides when nutty quits, not the last window.
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/nutty/appicon/nutty.svg")));

    nutty::Options options = nutty::parseOptions();
    // The Quick Controls style must be set before the first engine exists.
    nutty::selectQuickControlsStyle();

    nutty::Application nuttyApp(std::move(options));
    return nuttyApp.run();
}
