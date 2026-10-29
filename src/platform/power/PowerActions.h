// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Types.h"

#include <QString>

#include <expected>

namespace nutty {

struct PowerCapability {
    bool available = false;
    QString reason;  // why it isn't available (or a caveat when it is)
};

// Puts the machine to sleep, hibernates or shuts it down.
//   Linux:   systemd-logind over D-Bus (non-interactive, so polkit must allow it)
//   Windows: SetSuspendState / InitiateShutdownW
//   macOS:   pmset sleepnow / System Events "shut down" (hibernate unsupported)
namespace PowerActions {

// Whether an action can run here. Notify and Command are always available.
PowerCapability capability(ActionType action);

// Runs Sleep, Hibernate or Shutdown. Returns once the OS accepted (or
// refused) the request; the OS may still take a while to act.
std::expected<void, QString> execute(ActionType action);

// Starts a shell command detached (/bin/sh -c on Unix, cmd /C on Windows).
// Under Flatpak it runs inside the sandbox.
std::expected<void, QString> runCommand(const QString& command);

// True when running inside a Flatpak sandbox.
bool isSandboxed();

// macOS: sends a harmless Apple Event to System Events so the Automation
// permission prompt appears while someone is at the machine, instead of at
// shutdown time. A no-op elsewhere.
std::expected<void, QString> requestShutdownPermission();

}  // namespace PowerActions

}  // namespace nutty
