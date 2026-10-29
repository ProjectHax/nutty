// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/SecretStore.h"

#include <QHash>
#include <QString>

namespace nutty {

// The fallback SecretStore for systems without a usable keychain: a JSON file
// readable only by the owner (0600). Values are base64-encoded so they don't
// show up at a glance, which is obfuscation, not security; isSecure() is false
// so Settings can say so.
class FileSecretStore final : public SecretStore
{
public:
    explicit FileSecretStore(QString path);

    bool isSecure() const override { return false; }
    QString backendName() const override;

    std::expected<std::optional<QString>, QString> read(const QString& key) override;
    std::expected<void, QString> write(const QString& key, const QString& value) override;
    std::expected<void, QString> remove(const QString& key) override;

    QString path() const { return m_path; }

private:
    std::expected<void, QString> load();
    std::expected<void, QString> persist();

    QString m_path;
    QHash<QString, QString> m_values;
    bool m_loaded = false;
};

}  // namespace nutty
