// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/notify/DesktopNotifier.h"

#include <QSystemTrayIcon>

// Shared half of the desktop notifier: the tray fallback and registration.
// Construction and native delivery live in DesktopNotifier_{linux,win,mac}.

namespace nutty {

void DesktopNotifier::send(const Event& event)
{
    if (sendNative(event))
        return;
    if (sendThroughTray(event))
        emit sent(channelId());
    else
        emit failed(channelId(), tr("No desktop notification service is available."));
}

bool DesktopNotifier::sendThroughTray(const Event& event)
{
    auto* tray = m_tray ? m_tray() : nullptr;
    if (!tray || !tray->isVisible() || !QSystemTrayIcon::supportsMessages())
        return false;
    QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Information;
    if (event.severity == Severity::Warning)
        icon = QSystemTrayIcon::Warning;
    else if (event.severity == Severity::Critical)
        icon = QSystemTrayIcon::Critical;
    tray->showMessage(event.title, event.message, icon, event.severity == Severity::Critical ? 30000 : 10000);
    return true;
}

void DesktopNotifier::deliveredNatively(bool ok, const Event& event, const QString& error)
{
    if (ok) {
        emit sent(channelId());
        return;
    }
    if (sendThroughTray(event))
        emit sent(channelId());
    else
        emit failed(channelId(), error);
}

void registerDesktopNotifier(TrayIconProvider trayProvider)
{
    NotifierRegistry::registerFactory(ChannelType::Desktop,
        [trayProvider](const ChannelConfig& config, QObject* parent) -> std::unique_ptr<Notifier> {
            return std::make_unique<DesktopNotifier>(config, trayProvider, parent);
        });
}

}  // namespace nutty
