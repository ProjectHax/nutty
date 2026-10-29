// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/SecretStore.h"

#include <memory>

namespace nutty {

// The OS keychain:
//   Linux:   Secret Service (GNOME Keyring, KWallet, KeePassXC) over D-Bus,
//            items in the default collection with attribute key=<key>
//   Windows: Credential Manager, generic credentials named "nutty/<key>"
//   macOS:   Keychain, generic passwords with service = NUTTY_APP_ID
// Returns nullptr when no keychain is reachable; the caller then falls back
// to the file store from nutty_storage.
std::unique_ptr<SecretStore> createSystemSecretStore();

}  // namespace nutty
