// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/appearance/SystemAppearance.h"

#include <QMetaObject>
#include <QPointer>

#import <AppKit/AppKit.h>

// AppleInterfaceStyle is the user's choice, unaffected by NSApp.appearance,
// which Qt changes when the app forces a scheme.

namespace nutty {

namespace {

bool readDark()
{
    NSString* style = [[NSUserDefaults standardUserDefaults] stringForKey:@"AppleInterfaceStyle"];
    // Unset in light mode; messaging nil would yield NSOrderedSame (0).
    return style != nil && [style caseInsensitiveCompare:@"Dark"] == NSOrderedSame;
}

QColor readAccentColor()
{
    if (@available(macOS 10.14, *)) {
        NSColor* color = [[NSColor controlAccentColor] colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
        if (color)
            return QColor::fromRgbF(
                float(color.redComponent), float(color.greenComponent), float(color.blueComponent));
    }
    return {};
}

bool readReduceMotion()
{
    return [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion];
}

}  // namespace

struct SystemAppearance::Private {
    id themeObserver = nil;
    id colorsObserver = nil;
    id accessibilityObserver = nil;
};

SystemAppearance::SystemAppearance(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    watchQtColorScheme();
    updateDark(readDark());
    updateAccentColor(readAccentColor());
    updateReduceMotion(readReduceMotion());

    // Observers fire on the main queue; hop through Qt's loop so the
    // defaults have settled before reading them.
    QPointer<SystemAppearance> self = this;
    const auto later = [self](void (*apply)(SystemAppearance*)) {
        QMetaObject::invokeMethod(
            self.data(),
            [self, apply] {
                if (self)
                    apply(self.data());
            },
            Qt::QueuedConnection);
    };

    d->themeObserver = [[NSDistributedNotificationCenter defaultCenter]
        addObserverForName:@"AppleInterfaceThemeChangedNotification"
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification*) {
                    later([](SystemAppearance* q) { q->updateDark(readDark()); });
                }];
    d->colorsObserver = [[NSNotificationCenter defaultCenter]
        addObserverForName:NSSystemColorsDidChangeNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification*) {
                    later([](SystemAppearance* q) { q->updateAccentColor(readAccentColor()); });
                }];
    d->accessibilityObserver = [[[NSWorkspace sharedWorkspace] notificationCenter]
        addObserverForName:NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification*) {
                    later([](SystemAppearance* q) { q->updateReduceMotion(readReduceMotion()); });
                }];
}

SystemAppearance::~SystemAppearance()
{
    if (d->themeObserver)
        [[NSDistributedNotificationCenter defaultCenter] removeObserver:d->themeObserver];
    if (d->colorsObserver)
        [[NSNotificationCenter defaultCenter] removeObserver:d->colorsObserver];
    if (d->accessibilityObserver)
        [[[NSWorkspace sharedWorkspace] notificationCenter] removeObserver:d->accessibilityObserver];
}

}  // namespace nutty
