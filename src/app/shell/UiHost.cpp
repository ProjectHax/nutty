// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "app/shell/UiHost.h"

#include "app/ui/UiSetup.h"
#include "app/viewmodels/ThemeBridge.h"
#include "platform/window/WindowEffects.h"

#include <QLoggingCategory>
#include <QPixmapCache>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QTimer>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

Q_LOGGING_CATEGORY(lcUi, "nutty.ui")

namespace nutty {

UiHost::UiHost(QObject* parent)
    : QObject(parent)
{
    connect(ThemeBridge::instance(), &ThemeBridge::changed, this, [this] {
        // The native backdrop follows dark/light.
        if (m_main && m_main->isVisible())
            applyEffects(m_main);
    });
}

UiHost::~UiHost()
{
    delete m_countdown;
    delete m_main;
    m_engine.reset();
}

void UiHost::setKeepLoaded(bool keep)
{
    if (keep == m_keepLoaded)
        return;
    m_keepLoaded = keep;
    if (!keep)
        QTimer::singleShot(0, this, &UiHost::releaseIfIdle);
}

void UiHost::setTranslucent(bool translucent)
{
    if (translucent == m_translucent)
        return;
    m_translucent = translucent;
    if (m_main && m_main->isVisible())
        applyEffects(m_main);
}

QQmlEngine* UiHost::engine()
{
    if (!m_engine) {
        m_engine = std::make_unique<QQmlEngine>();
        setupQmlEngine(*m_engine);
        connect(m_engine.get(), &QQmlEngine::quit, this, &UiHost::quitRequested);
        qCDebug(lcUi) << "QML engine created";
    }
    return m_engine.get();
}

QQuickWindow* UiHost::create(const QString& file, const QVariantMap& properties)
{
    QQmlComponent component(engine(), QUrl(QStringLiteral("qrc:/qt/qml/Nutty/qml/") + file));
    if (component.isError()) {
        qCWarning(lcUi).noquote() << component.errorString();
        return nullptr;
    }
    QObject* object = component.createWithInitialProperties(properties);
    auto* window = qobject_cast<QQuickWindow*>(object);
    if (!window) {
        qCWarning(lcUi).noquote() << file << "did not create a window:" << component.errorString();
        delete object;
        return nullptr;
    }
    QQmlEngine::setObjectOwnership(window, QQmlEngine::CppOwnership);
    watchRenderer(window);
    return window;
}

void UiHost::applyEffects(QQuickWindow* window)
{
    if (!window)
        return;
    WindowEffectOptions options;
    options.translucent = m_translucent;
    options.dark = ThemeBridge::instance()->dark();
    const bool backdrop = WindowEffects::apply(window, options);
    window->setProperty("backdrop", backdrop);
    const WindowEffects::TitleBarOverlay overlay = WindowEffects::titleBarOverlay(window);
    window->setProperty("titleBarInset", overlay.height);
    window->setProperty("windowButtonsWidth", overlay.buttonsWidth);
    window->setProperty("windowButtonsOnRight", overlay.buttonsOnRight);
}

void UiHost::bringToFront(QQuickWindow* window)
{
    if (window->windowState() & Qt::WindowMinimized)
        window->setWindowStates(window->windowStates() & ~Qt::WindowMinimized);
    window->show();
    window->raise();
    window->requestActivate();
}

void UiHost::showMain(const QString& page, const QString& id, const QSize& size)
{
    if (!m_main) {
        QVariantMap properties;
        if (!page.isEmpty())
            properties.insert(QStringLiteral("page"), page);
        if (!id.isEmpty())
            properties.insert(QStringLiteral("deviceId"), id);
        if (size.isValid()) {
            properties.insert(QStringLiteral("width"), size.width());
            properties.insert(QStringLiteral("height"), size.height());
        }
        m_main = create(QStringLiteral("Main.qml"), properties);
        if (!m_main)
            return;
        connect(m_main, &QWindow::visibleChanged, this, &UiHost::onMainVisibleChanged);
        emit windowsChanged();
    } else if (!page.isEmpty()) {
        QMetaObject::invokeMethod(m_main, "openPage", Q_ARG(QVariant, page), Q_ARG(QVariant, id));
    }
    const bool wasVisible = m_main->isVisible();
    bringToFront(m_main);
    applyEffects(m_main);
    if (!wasVisible)
        emit mainShown();
}

void UiHost::toggleMain()
{
    if (m_main && m_main->isVisible() && m_main->isActive())
        hideMain();
    else
        showMain();
}

void UiHost::hideMain()
{
    if (m_main)
        m_main->close();
}

bool UiHost::mainVisible() const
{
    return m_main && m_main->isVisible();
}

void UiHost::onMainVisibleChanged(bool visible)
{
    if (visible)
        return;
    emit mainHidden();
    if (m_quitOnClose) {
        emit quitRequested();
        return;
    }
    QTimer::singleShot(0, this, &UiHost::releaseIfIdle);
}

void UiHost::showCountdown()
{
    if (!m_countdown) {
        m_countdown = create(QStringLiteral("CountdownAlert.qml"), {});
        if (!m_countdown)
            return;
        connect(m_countdown, &QWindow::visibleChanged, this, [this](bool visible) {
            if (!visible)
                QTimer::singleShot(0, this, &UiHost::releaseIfIdle);
        });
        emit windowsChanged();
    }
    bringToFront(m_countdown);
}

void UiHost::closeCountdown()
{
    if (m_countdown)
        m_countdown->close();
}

void UiHost::releaseIfIdle()
{
    bool changed = false;
    if (m_countdown && !m_countdown->isVisible()) {
        delete m_countdown;
        changed = true;
    }
    if (m_main && !m_main->isVisible() && !m_keepLoaded) {
        delete m_main;
        changed = true;
    }
    if (changed)
        emit windowsChanged();
    if (m_main || m_countdown || !m_engine)
        return;
    m_engine.reset();
    QPixmapCache::clear();
    qCDebug(lcUi) << "QML engine released";
#if defined(__GLIBC__)
    // Hand the freed heap back to the system so the tray-only RSS drops.
    malloc_trim(0);
#endif
}

void UiHost::retranslate()
{
    if (m_engine)
        m_engine->retranslate();
}

}  // namespace nutty
