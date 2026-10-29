// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/secrets/SystemSecretStore.h"

#include <QCoreApplication>

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

namespace nutty {

namespace {

// Releases a CoreFoundation object at scope exit.
template <typename T> class CFRef
{
public:
    explicit CFRef(T ref = nullptr)
        : m_ref(ref)
    {
    }
    ~CFRef()
    {
        if (m_ref)
            CFRelease(m_ref);
    }
    CFRef(const CFRef&) = delete;
    CFRef& operator=(const CFRef&) = delete;

    T get() const { return m_ref; }
    T* out() { return &m_ref; }

private:
    T m_ref;
};

QString statusMessage(OSStatus status)
{
    CFRef<CFStringRef> text(SecCopyErrorMessageString(status, nullptr));
    if (text.get())
        return QString::fromCFString(text.get());
    return QStringLiteral("OSStatus %1").arg(status);
}

// A query dictionary for the generic password service = NUTTY_APP_ID, account = key.
CFMutableDictionaryRef baseQuery(CFStringRef service, CFStringRef account)
{
    CFMutableDictionaryRef query = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, service);
    CFDictionarySetValue(query, kSecAttrAccount, account);
    return query;
}

class KeychainStore : public SecretStore
{
public:
    bool isSecure() const override { return true; }
    QString backendName() const override
    {
        return QCoreApplication::translate("nutty::SecretStore", "macOS Keychain");
    }

    std::expected<std::optional<QString>, QString> read(const QString& key) override
    {
        CFRef<CFStringRef> service(QStringLiteral(NUTTY_APP_ID).toCFString());
        CFRef<CFStringRef> account(key.toCFString());
        CFRef<CFMutableDictionaryRef> query(baseQuery(service.get(), account.get()));
        CFDictionarySetValue(query.get(), kSecReturnData, kCFBooleanTrue);
        CFDictionarySetValue(query.get(), kSecMatchLimit, kSecMatchLimitOne);

        CFRef<CFTypeRef> result;
        const OSStatus status = SecItemCopyMatching(query.get(), result.out());
        if (status == errSecItemNotFound)
            return std::optional<QString>();
        if (status != errSecSuccess)
            return std::unexpected(statusMessage(status));
        const auto data = QByteArray::fromCFData(static_cast<CFDataRef>(result.get()));
        return std::optional<QString>(QString::fromUtf8(data));
    }

    std::expected<void, QString> write(const QString& key, const QString& value) override
    {
        CFRef<CFStringRef> service(QStringLiteral(NUTTY_APP_ID).toCFString());
        CFRef<CFStringRef> account(key.toCFString());
        CFRef<CFDataRef> data(value.toUtf8().toCFData());
        CFRef<CFMutableDictionaryRef> query(baseQuery(service.get(), account.get()));

        CFRef<CFMutableDictionaryRef> update(CFDictionaryCreateMutable(
            kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
        CFDictionarySetValue(update.get(), kSecValueData, data.get());
        OSStatus status = SecItemUpdate(query.get(), update.get());
        if (status == errSecItemNotFound) {
            CFDictionarySetValue(query.get(), kSecValueData, data.get());
            CFDictionarySetValue(query.get(), kSecAttrAccessible, kSecAttrAccessibleAfterFirstUnlock);
            status = SecItemAdd(query.get(), nullptr);
        }
        if (status != errSecSuccess)
            return std::unexpected(statusMessage(status));
        return {};
    }

    std::expected<void, QString> remove(const QString& key) override
    {
        CFRef<CFStringRef> service(QStringLiteral(NUTTY_APP_ID).toCFString());
        CFRef<CFStringRef> account(key.toCFString());
        CFRef<CFMutableDictionaryRef> query(baseQuery(service.get(), account.get()));
        const OSStatus status = SecItemDelete(query.get());
        if (status != errSecSuccess && status != errSecItemNotFound)
            return std::unexpected(statusMessage(status));
        return {};
    }
};

}  // namespace

std::unique_ptr<SecretStore> createSystemSecretStore()
{
    return std::make_unique<KeychainStore>();
}

}  // namespace nutty
