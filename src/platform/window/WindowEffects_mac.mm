// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/window/WindowEffects.h"

#include <QWindow>

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
    NSView* qtView = reinterpret_cast<NSView*>(window->winId());
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
    if (@available(macOS 10.14, *))
        effect.material = NSVisualEffectMaterialUnderWindowBackground;
    else
        effect.material = NSVisualEffectMaterialAppearanceBased;
    nsWindow.opaque = NO;
    nsWindow.backgroundColor = [NSColor clearColor];
    return true;
}

}  // namespace nutty::WindowEffects
