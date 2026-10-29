// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Notifier.h"

#include <functional>
#include <memory>

class QSystemTrayIcon;

namespace nutty {

// Returns the app's tray icon (or nullptr), used for showMessage() balloons.
using TrayIconProvider = std::function<QSystemTrayIcon*()>;

// Desktop notifications.
//   Linux:   org.freedesktop.Notifications, with "Cancel" on cancellable
//            ActionScheduled events and "default" (click) reported as "open"
//   Windows: QSystemTrayIcon::showMessage (toasts have no buttons)
//   macOS:   UNUserNotificationCenter when running from a signed bundle
// Each falls back to the tray icon's balloon when the native service is missing.
class DesktopNotifier : public Notifier
{
    Q_OBJECT

public:
    DesktopNotifier(ChannelConfig config, TrayIconProvider tray, QObject* parent = nullptr);
    ~DesktopNotifier() override;

    void send(const Event& event) override;

    // Linux: called from the D-Bus listener when the user picks an action.
    void handleAction(quint32 notificationId, const QString& actionKey);
    void handleClosed(quint32 notificationId);

private:
    // Native delivery. Returns false to fall back to the tray right away;
    // when it returns true it reports sent()/failed() itself (possibly later).
    bool sendNative(const Event& event);
    // Shows a tray balloon; false if there is no usable tray icon.
    bool sendThroughTray(const Event& event);
    void deliveredNatively(bool ok, const Event& event, const QString& error);

    struct Private;
    std::unique_ptr<Private> d;
    TrayIconProvider m_tray;
};

// Registers ChannelType::Desktop with NotifierRegistry.
void registerDesktopNotifier(TrayIconProvider trayProvider);

}  // namespace nutty
