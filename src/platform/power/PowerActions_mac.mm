// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/power/PowerActions.h"

#include <QCoreApplication>
#include <QProcess>

#import <Foundation/Foundation.h>

namespace nutty::PowerActions {

namespace {

// Runs an AppleScript in-process (needs the apple-events entitlement under
// the hardened runtime, plus the user's Automation consent for System Events).
std::expected<void, QString> runAppleScript(NSString* source)
{
    @autoreleasepool {
        NSAppleScript* script = [[NSAppleScript alloc] initWithSource:source];
        NSDictionary* error = nil;
        [script executeAndReturnError:&error];
        if (error) {
            NSString* message = error[NSAppleScriptErrorMessage];
            if (message)
                return std::unexpected(QString::fromNSString(message));
            return std::unexpected(QCoreApplication::translate("nutty::PowerActions",
                "macOS refused the request. Check System Settings > Privacy & Security > Automation."));
        }
        return {};
    }
}

}  // namespace

PowerCapability capability(ActionType action)
{
    switch (action) {
    case ActionType::Notify:
    case ActionType::Command:
    case ActionType::Sleep:
        return { true, {} };
    case ActionType::Shutdown:
        return { true,
            QCoreApplication::translate(
                "nutty::PowerActions", "macOS asks once for permission to control System Events.") };
    case ActionType::Hibernate:
        return { false,
            QCoreApplication::translate("nutty::PowerActions", "macOS doesn't let apps hibernate the Mac.") };
    }
    return { false, {} };
}

std::expected<void, QString> execute(ActionType action)
{
    switch (action) {
    case ActionType::Sleep: {
        QProcess pmset;
        pmset.start(QStringLiteral("/usr/bin/pmset"), { QStringLiteral("sleepnow") });
        if (!pmset.waitForFinished(10000) || pmset.exitStatus() != QProcess::NormalExit
            || pmset.exitCode() != 0)
            return std::unexpected(
                QCoreApplication::translate("nutty::PowerActions", "pmset could not put the Mac to sleep."));
        return {};
    }
    case ActionType::Shutdown:
        return runAppleScript(@"tell application \"System Events\" to shut down");
    case ActionType::Hibernate:
        return std::unexpected(
            QCoreApplication::translate("nutty::PowerActions", "macOS doesn't let apps hibernate the Mac."));
    case ActionType::Notify:
    case ActionType::Command:
        break;
    }
    return std::unexpected(QCoreApplication::translate("nutty::PowerActions", "Not a power action."));
}

std::expected<void, QString> requestShutdownPermission()
{
    return runAppleScript(@"tell application \"System Events\" to return name of current user");
}

}  // namespace nutty::PowerActions
