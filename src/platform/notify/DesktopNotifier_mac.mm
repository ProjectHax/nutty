// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/notify/DesktopNotifier.h"

#include <QMetaObject>
#include <QPointer>

#import <Foundation/Foundation.h>
#import <UserNotifications/UserNotifications.h>

// Shows banners even while nutty is the active app (macOS hides them by default).
@interface NuttyNotificationDelegate : NSObject <UNUserNotificationCenterDelegate>
@end

@implementation NuttyNotificationDelegate
- (void)userNotificationCenter:(UNUserNotificationCenter*)center
       willPresentNotification:(UNNotification*)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completionHandler
{
    completionHandler(UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionList
                      | UNNotificationPresentationOptionSound);
}
@end

namespace nutty {

namespace {

// UNUserNotificationCenter throws when the process isn't a bundled app.
bool canUseUserNotifications()
{
    return [[NSBundle mainBundle] bundleIdentifier] != nil;
}

void setUpCenterOnce()
{
    static NuttyNotificationDelegate* delegate = nil;
    if (delegate)
        return;
    delegate = [[NuttyNotificationDelegate alloc] init];
    UNUserNotificationCenter* center = [UNUserNotificationCenter currentNotificationCenter];
    center.delegate = delegate;
    [center requestAuthorizationWithOptions:(UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
                          completionHandler:^(BOOL, NSError*) {}];
}

}  // namespace

struct DesktopNotifier::Private { };

DesktopNotifier::DesktopNotifier(ChannelConfig config, TrayIconProvider tray, QObject* parent)
    : Notifier(std::move(config), parent)
    , d(std::make_unique<Private>())
    , m_tray(std::move(tray))
{
}

DesktopNotifier::~DesktopNotifier() = default;

bool DesktopNotifier::sendNative(const Event& event)
{
    if (!canUseUserNotifications())
        return false;
    setUpCenterOnce();

    UNMutableNotificationContent* content = [[UNMutableNotificationContent alloc] init];
    content.title = event.title.toNSString();
    content.body = event.message.toNSString();
    if (event.severity != Severity::Info)
        content.sound = [UNNotificationSound defaultSound];

    UNNotificationRequest* request = [UNNotificationRequest requestWithIdentifier:[[NSUUID UUID] UUIDString]
                                                                          content:content
                                                                          trigger:nil];
    QPointer<DesktopNotifier> self = this;
    const Event copy = event;
    [[UNUserNotificationCenter currentNotificationCenter]
        addNotificationRequest:request
         withCompletionHandler:^(NSError* error) {
             const QString message = error ? QString::fromNSString(error.localizedDescription) : QString();
             // The handler runs on a background queue.
             QMetaObject::invokeMethod(
                 self.data(),
                 [self, copy, message]() {
                     if (self)
                         self->deliveredNatively(message.isEmpty(), copy, message);
                 },
                 Qt::QueuedConnection);
         }];
    return true;
}

void DesktopNotifier::handleAction(quint32, const QString&) { }

void DesktopNotifier::handleClosed(quint32) { }

}  // namespace nutty
