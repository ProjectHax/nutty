// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/mac/MacActivation.h"

#import <AppKit/AppKit.h>

namespace nutty::MacActivation {

void setRegularActivationPolicy(bool regular)
{
    const NSApplicationActivationPolicy policy
        = regular ? NSApplicationActivationPolicyRegular : NSApplicationActivationPolicyAccessory;
    if (NSApp.activationPolicy == policy)
        return;
    [NSApp setActivationPolicy:policy];
    // A newly regular app stays behind the frontmost one unless activated.
    if (regular) {
#if defined(__MAC_14_0) && __MAC_OS_X_VERSION_MAX_ALLOWED >= __MAC_14_0
        if (@available(macOS 14.0, *)) {
            [NSApp activate];
            return;
        }
#endif
        [NSApp activateIgnoringOtherApps:YES];
    }
}

}  // namespace nutty::MacActivation
