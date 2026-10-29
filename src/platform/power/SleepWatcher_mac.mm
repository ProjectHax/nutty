// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/power/SleepWatcher.h"

#include <QPointer>

#import <AppKit/AppKit.h>

namespace nutty {

struct SleepWatcher::Private {
    id willSleep = nil;
    id didWake = nil;
};

SleepWatcher::SleepWatcher(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    // NSWorkspace posts these on the main queue, which is Qt's GUI thread.
    NSNotificationCenter* center = [[NSWorkspace sharedWorkspace] notificationCenter];
    QPointer<SleepWatcher> self = this;
    d->willSleep = [center addObserverForName:NSWorkspaceWillSleepNotification
                                       object:nil
                                        queue:[NSOperationQueue mainQueue]
                                   usingBlock:^(NSNotification*) {
                                       if (self)
                                           self->notifySleeping();
                                   }];
    d->didWake = [center addObserverForName:NSWorkspaceDidWakeNotification
                                     object:nil
                                      queue:[NSOperationQueue mainQueue]
                                 usingBlock:^(NSNotification*) {
                                     if (self)
                                         self->notifyResumed();
                                 }];
}

SleepWatcher::~SleepWatcher()
{
    NSNotificationCenter* center = [[NSWorkspace sharedWorkspace] notificationCenter];
    if (d->willSleep)
        [center removeObserver:d->willSleep];
    if (d->didWake)
        [center removeObserver:d->didWake];
}

bool SleepWatcher::isSupported() const
{
    return true;
}

}  // namespace nutty
