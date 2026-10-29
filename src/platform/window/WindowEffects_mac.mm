// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/window/WindowEffects.h"

#include <QWindow>

#include <cmath>

#import <AppKit/AppKit.h>

namespace nutty::WindowEffects {

namespace {

// Tags the effect view so a second apply() finds it instead of adding another.
NSUserInterfaceItemIdentifier const kEffectViewId = @"nutty.backdrop";

NSVisualEffectView* findEffectView(NSView* container)
{
    for (NSView* view in container.subviews) {
        if ([view.identifier isEqualToString:kEffectViewId]
            && [view isKindOfClass:[NSVisualEffectView class]])
            return (NSVisualEffectView*)view;
    }
    return nil;
}

}  // namespace

bool apply(QWindow* window, const WindowEffectOptions& options)
{
    if (!window)
        return false;
    NSView* qtView = (__bridge NSView*)reinterpret_cast<void*>(window->winId());
    NSWindow* nsWindow = qtView.window;
    if (!nsWindow)
        return false;

    nsWindow.appearance
        = [NSAppearance appearanceNamed:options.dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
    nsWindow.titlebarAppearsTransparent = YES;
    nsWindow.titleVisibility = NSWindowTitleHidden;
    nsWindow.styleMask |= NSWindowStyleMaskFullSizeContentView;

    // The effect view sits next to Qt's view, behind it, in the window's frame view.
    NSView* container = nsWindow.contentView.superview;
    NSVisualEffectView* effect = container ? findEffectView(container) : nil;
    if (!options.translucent || !container) {
        [effect removeFromSuperview];
        nsWindow.opaque = YES;
        nsWindow.backgroundColor = [NSColor windowBackgroundColor];
        return false;
    }

    if (!effect) {
        effect = [[NSVisualEffectView alloc] initWithFrame:container.bounds];
        effect.identifier = kEffectViewId;
        effect.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        effect.blendingMode = NSVisualEffectBlendingModeBehindWindow;
        effect.state = NSVisualEffectStateFollowsWindowActiveState;
        [container addSubview:effect positioned:NSWindowBelow relativeTo:nsWindow.contentView];
    }
    effect.material = NSVisualEffectMaterialUnderWindowBackground;
    nsWindow.opaque = NO;
    nsWindow.backgroundColor = [NSColor clearColor];
    return true;
}

TitleBarOverlay titleBarOverlay(QWindow* window)
{
    if (!window)
        return {};
    NSView* qtView = (__bridge NSView*)reinterpret_cast<void*>(window->winId());
    NSWindow* nsWindow = qtView.window;
    if (!nsWindow || !(nsWindow.styleMask & NSWindowStyleMaskFullSizeContentView))
        return {};
    TitleBarOverlay overlay;
    // The layout rect is the part of the window the title bar doesn't cover.
    overlay.height
        = static_cast<int>(std::ceil(NSHeight(nsWindow.frame) - NSHeight(nsWindow.contentLayoutRect)));

    NSRect buttons = NSZeroRect;
    for (NSWindowButton kind : { NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton }) {
        NSButton* button = [nsWindow standardWindowButton:kind];
        if (button && !button.hidden)
            buttons = NSUnionRect(buttons, [button convertRect:button.bounds toView:nil]);
    }
    if (NSIsEmptyRect(buttons))
        return overlay;
    overlay.buttonsOnRight = NSMidX(buttons) > NSWidth(nsWindow.frame) / 2;
    const CGFloat margin
        = overlay.buttonsOnRight ? NSWidth(nsWindow.frame) - NSMaxX(buttons) : NSMinX(buttons);
    overlay.buttonsWidth = static_cast<int>(std::ceil(NSWidth(buttons) + 2 * margin));
    return overlay;
}

}  // namespace nutty::WindowEffects
