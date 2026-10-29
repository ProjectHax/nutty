// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include <QString>

#include <expected>
#include <optional>

namespace nutty {

// Stores credentials outside the config file. nutty_platform provides the OS
// keychain backends (Credential Manager, Keychain, Secret Service);
// nutty_storage provides the file fallback used when no keychain exists.
//
// Keys look like "device/<uuid>/password" or "channel/<uuid>/apiToken".
// Calls are synchronous; a backend may block briefly (e.g. an unlock prompt).
class SecretStore
{
public:
    virtual ~SecretStore() = default;

    // False for the plain-file fallback, so Settings can warn about it.
    virtual bool isSecure() const = 0;
    virtual QString backendName() const = 0;

    // std::nullopt when the key doesn't exist; an error string on failure.
    virtual std::expected<std::optional<QString>, QString> read(const QString& key) = 0;
    virtual std::expected<void, QString> write(const QString& key, const QString& value) = 0;
    // Removing a missing key is not an error.
    virtual std::expected<void, QString> remove(const QString& key) = 0;
};

}  // namespace nutty
