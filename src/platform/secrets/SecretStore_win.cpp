// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/secrets/SystemSecretStore.h"

#include <QCoreApplication>

#include <windows.h>
// wincred.h relies on the types from windows.h.
#include <wincred.h>

namespace nutty {

namespace {

std::wstring targetFor(const QString& key)
{
    return (QStringLiteral("nutty/") + key).toStdWString();
}

QString lastErrorMessage(DWORD error)
{
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
        error, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    QString message = length ? QString::fromWCharArray(buffer, int(length)).trimmed()
                             : QStringLiteral("error %1").arg(error);
    LocalFree(buffer);
    return message;
}

// Windows Credential Manager. The value is stored as UTF-8 in the blob.
class CredentialManagerStore : public SecretStore
{
public:
    bool isSecure() const override { return true; }
    QString backendName() const override
    {
        return QCoreApplication::translate("nutty::SecretStore", "Windows Credential Manager");
    }

    std::expected<std::optional<QString>, QString> read(const QString& key) override
    {
        const auto target = targetFor(key);
        PCREDENTIALW credential = nullptr;
        if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
            const DWORD error = GetLastError();
            if (error == ERROR_NOT_FOUND)
                return std::optional<QString>();
            return std::unexpected(lastErrorMessage(error));
        }
        const auto value = QString::fromUtf8(reinterpret_cast<const char*>(credential->CredentialBlob),
            qsizetype(credential->CredentialBlobSize));
        CredFree(credential);
        return std::optional<QString>(value);
    }

    std::expected<void, QString> write(const QString& key, const QString& value) override
    {
        QByteArray blob = value.toUtf8();
        if (blob.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE)
            return std::unexpected(QCoreApplication::translate(
                "nutty::SecretStore", "The value is too long for the Credential Manager."));
        auto target = targetFor(key);
        std::wstring user = L"nutty";

        CREDENTIALW credential {};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = target.data();
        credential.UserName = user.data();
        credential.CredentialBlobSize = DWORD(blob.size());
        credential.CredentialBlob = reinterpret_cast<LPBYTE>(blob.data());
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        if (!CredWriteW(&credential, 0))
            return std::unexpected(lastErrorMessage(GetLastError()));
        return {};
    }

    std::expected<void, QString> remove(const QString& key) override
    {
        const auto target = targetFor(key);
        if (!CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0)) {
            const DWORD error = GetLastError();
            if (error != ERROR_NOT_FOUND)
                return std::unexpected(lastErrorMessage(error));
        }
        return {};
    }
};

}  // namespace

std::unique_ptr<SecretStore> createSystemSecretStore()
{
    return std::make_unique<CredentialManagerStore>();
}

}  // namespace nutty
