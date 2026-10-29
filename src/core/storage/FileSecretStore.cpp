// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/storage/FileSecretStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace nutty {

namespace {

constexpr auto kOwnerOnly = QFileDevice::ReadOwner | QFileDevice::WriteOwner;

}  // namespace

FileSecretStore::FileSecretStore(QString path)
    : m_path(std::move(path))
{
}

QString FileSecretStore::backendName() const
{
    return QCoreApplication::translate("nutty::Storage", "Local file");
}

std::expected<void, QString> FileSecretStore::load()
{
    if (m_loaded)
        return {};
    QFile file(m_path);
    if (!file.exists()) {
        m_loaded = true;
        return {};
    }
    if (!file.open(QIODevice::ReadOnly))
        return std::unexpected(file.errorString());
    // Tighten permissions in case something loosened them.
    file.setPermissions(kOwnerOnly);

    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError)
        return std::unexpected(error.errorString());
    const auto object = doc.object();
    for (auto it = object.begin(); it != object.end(); ++it) {
        const auto decoded = QByteArray::fromBase64(it.value().toString().toLatin1());
        m_values.insert(it.key(), QString::fromUtf8(decoded));
    }
    m_loaded = true;
    return {};
}

std::expected<void, QString> FileSecretStore::persist()
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QJsonObject object;
    for (auto it = m_values.cbegin(); it != m_values.cend(); ++it)
        object.insert(it.key(), QString::fromLatin1(it.value().toUtf8().toBase64()));

    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return std::unexpected(file.errorString());
    file.setPermissions(kOwnerOnly);
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (!file.commit())
        return std::unexpected(file.errorString());
    QFile::setPermissions(m_path, kOwnerOnly);
    return {};
}

std::expected<std::optional<QString>, QString> FileSecretStore::read(const QString& key)
{
    if (auto loaded = load(); !loaded)
        return std::unexpected(loaded.error());
    const auto it = m_values.constFind(key);
    if (it == m_values.cend())
        return std::optional<QString> {};
    return std::optional<QString> { *it };
}

std::expected<void, QString> FileSecretStore::write(const QString& key, const QString& value)
{
    if (auto loaded = load(); !loaded)
        return std::unexpected(loaded.error());
    if (m_values.value(key) == value && m_values.contains(key))
        return {};
    m_values.insert(key, value);
    return persist();
}

std::expected<void, QString> FileSecretStore::remove(const QString& key)
{
    if (auto loaded = load(); !loaded)
        return std::unexpected(loaded.error());
    if (!m_values.remove(key))
        return {};
    return persist();
}

}  // namespace nutty
