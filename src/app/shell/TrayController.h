// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "app/shell/TrayIconEngine.h"

#include <QObject>
#include <QPointer>
#include <QTimer>

#include <memory>

class QAction;
class QMenu;
class QSystemTrayIcon;

namespace nutty {

class AppContext;

// The tray (menu bar) icon: a battery showing the worst device, a tooltip
// with a line per device, and a menu with the devices, the pending action
// and its Cancel, Snooze, Open and Quit.
class TrayController : public QObject
{
    Q_OBJECT

public:
    explicit TrayController(AppContext* context, QObject* parent = nullptr);
    ~TrayController() override;

    QSystemTrayIcon* icon() const { return m_icon; }
    void setStyle(TrayIconStyle style);
    void setMonochrome(bool monochrome);
    void setDarkPanel(bool dark);
    // Re-reads every string after a language change.
    void retranslate();
    TrayIconState state() const { return m_state; }

signals:
    void openRequested();
    void toggleRequested();
    void quitRequested();

private:
    void update();
    void scheduleUpdate();
    void rebuildDeviceActions();

    QPointer<AppContext> m_context;
    QSystemTrayIcon* m_icon = nullptr;
    std::unique_ptr<QMenu> m_menu;
    QList<QAction*> m_deviceActions;
    QAction* m_deviceSeparator = nullptr;
    QAction* m_open = nullptr;
    QAction* m_pending = nullptr;
    QAction* m_cancel = nullptr;
    QAction* m_snooze = nullptr;
    QAction* m_quit = nullptr;
    TrayIconState m_state;
    bool m_iconSet = false;
    QTimer m_debounce;
    QTimer m_ticker;  // the countdown line while a menu is open
};

}  // namespace nutty
