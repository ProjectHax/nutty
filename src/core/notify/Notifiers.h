// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

namespace nutty {

// Registers the network channels (Pushover, ntfy, email, webhook, PagerDuty) with
// NotifierRegistry. The desktop channel is registered by nutty_platform.
void registerCoreNotifiers();

}  // namespace nutty
