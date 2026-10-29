// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "app/ui/UiSetup.h"

#include "app/ui/SvgImageProvider.h"
#include "app/viewmodels/ThemeBridge.h"

#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>

#include <memory>

namespace nutty {

void selectQuickControlsStyle()
{
    if (qEnvironmentVariableIsSet("QT_QUICK_CONTROLS_STYLE"))
        return;
#if defined(Q_OS_WIN)
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QQuickStyle::setStyle(QStringLiteral("FluentWinUI3"));
#else
    QQuickStyle::setStyle(QStringLiteral("Universal"));
#endif
#elif defined(Q_OS_MACOS)
    QQuickStyle::setStyle(QStringLiteral("macOS"));
#else
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
#endif
}

void prepareQuickWindows()
{
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    QQuickWindow::setDefaultAlphaBuffer(true);
#endif
}

void watchRenderer(QQuickWindow* window)
{
    // Check once, after the first frame, when the scene graph has settled on
    // a backend. frameSwapped may come from the render thread.
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = QObject::connect(
        window, &QQuickWindow::frameSwapped, window,
        [window, connection] {
            QObject::disconnect(*connection);
            const auto api = window->rendererInterface()->graphicsApi();
            if (api != QSGRendererInterface::Software && api != QSGRendererInterface::Null)
                return;
            QMetaObject::invokeMethod(
                ThemeBridge::instance(), [] { ThemeBridge::instance()->setEffectsEnabled(false); },
                Qt::QueuedConnection);
        },
        Qt::DirectConnection);
}

void setupQmlEngine(QQmlEngine& engine)
{
#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
#endif
    engine.addImageProvider(QStringLiteral("icons"), new SvgImageProvider(SvgImageProvider::Kind::Icons));
    engine.addImageProvider(QStringLiteral("art"), new SvgImageProvider(SvgImageProvider::Kind::Art));
    if (auto* app = qobject_cast<QQmlApplicationEngine*>(&engine)) {
        QObject::connect(app, &QQmlApplicationEngine::objectCreated, app, [](QObject* object, const QUrl&) {
            if (auto* window = qobject_cast<QQuickWindow*>(object))
                watchRenderer(window);
        });
    }
}

}  // namespace nutty
