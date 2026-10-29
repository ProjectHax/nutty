// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QSize>
#include <QString>
#include <QVariantMap>

#include <memory>

class QQmlEngine;

namespace nutty {

// Owns the QML side and keeps it out of memory while nobody looks: the
// engine is created when the first window is needed and dropped when the
// last one closes (unless keepLoaded). Windows are the main window
// (Main.qml) and the power action countdown (CountdownAlert.qml).
class UiHost : public QObject
{
    Q_OBJECT

public:
    explicit UiHost(QObject* parent = nullptr);
    ~UiHost() override;

    // Keep the main window (and engine) alive when it's closed, for an
    // instant reopen at the cost of memory.
    void setKeepLoaded(bool keep);
    // Closing the main window quits (there is no tray to get it back).
    void setQuitOnClose(bool quit) { m_quitOnClose = quit; }
    bool quitOnClose() const { return m_quitOnClose; }
    void setTranslucent(bool translucent);

    // Shows (creating if needed) and raises the main window; page/id are
    // optional ("device" + a device id).
    void showMain(const QString& page = {}, const QString& id = {}, const QSize& size = {});
    void toggleMain();
    void hideMain();
    bool mainVisible() const;
    QQuickWindow* mainWindow() const { return m_main; }

    void showCountdown();
    void closeCountdown();
    QQuickWindow* countdownWindow() const { return m_countdown; }

    bool hasEngine() const { return m_engine != nullptr; }
    void retranslate();

signals:
    void quitRequested();
    void mainShown();
    void mainHidden();
    // A window exists (or went away); the app tracks this for macOS's Dock.
    void windowsChanged();

private:
    QQmlEngine* engine();
    QQuickWindow* create(const QString& file, const QVariantMap& properties);
    void onMainVisibleChanged(bool visible);
    void releaseIfIdle();
    void applyEffects(QQuickWindow* window);
    static void bringToFront(QQuickWindow* window);

    std::unique_ptr<QQmlEngine> m_engine;
    QPointer<QQuickWindow> m_main;
    QPointer<QQuickWindow> m_countdown;
    bool m_keepLoaded = false;
    bool m_quitOnClose = false;
    bool m_translucent = true;
    bool m_releaseQueued = false;
};

}  // namespace nutty
