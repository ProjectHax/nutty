// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/notify/DesktopNotifier.h"

// Windows turns QSystemTrayIcon::showMessage into a toast, so the tray path
// is the native one here.

namespace nutty {

struct DesktopNotifier::Private {
};

DesktopNotifier::DesktopNotifier(ChannelConfig config, TrayIconProvider tray, QObject* parent)
    : Notifier(std::move(config), parent)
    , d(std::make_unique<Private>())
    , m_tray(std::move(tray))
{
}

DesktopNotifier::~DesktopNotifier() = default;

bool DesktopNotifier::sendNative(const Event&)
{
    return false;
}

void DesktopNotifier::handleAction(quint32, const QString&)
{
}

void DesktopNotifier::handleClosed(quint32)
{
}

}  // namespace nutty
