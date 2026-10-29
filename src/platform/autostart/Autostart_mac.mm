// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/autostart/Autostart.h"

#include <QCoreApplication>

#import <Foundation/Foundation.h>
#import <ServiceManagement/ServiceManagement.h>

namespace nutty::Autostart {

bool isSupported()
{
    if (@available(macOS 13.0, *))
        return true;
    return false;
}

bool isEnabled()
{
    if (@available(macOS 13.0, *))
        return [SMAppService mainAppService].status == SMAppServiceStatusEnabled;
    return false;
}

std::expected<void, QString> setEnabled(bool enabled)
{
    if (@available(macOS 13.0, *)) {
        NSError* error = nil;
        SMAppService* service = [SMAppService mainAppService];
        if (!enabled && service.status != SMAppServiceStatusEnabled)
            return {};
        const BOOL ok
            = enabled ? [service registerAndReturnError:&error] : [service unregisterAndReturnError:&error];
        if (!ok && error)
            return std::unexpected(QString::fromNSString(error.localizedDescription));
        return {};
    }
    return std::unexpected(
        QCoreApplication::translate("nutty::Autostart", "Starting at login needs macOS 13 or later."));
}

}  // namespace nutty::Autostart
