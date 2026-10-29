// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/secrets/SystemSecretStore.h"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QEventLoop>
#include <QLoggingCategory>
#include <QMap>
#include <QTimer>

// The freedesktop Secret Service API (GNOME Keyring, KWallet, KeePassXC).
// Items live in the default collection and carry the attributes
// xdg:schema=<app id> and key=<key>. The session uses the "plain" algorithm:
// values travel over the user's session bus, which only that user can reach.

namespace nutty::secretservice {

// The (oayays) Secret struct.
struct Secret {
    QDBusObjectPath session;
    QByteArray parameters;
    QByteArray value;
    QString contentType;
};

QDBusArgument& operator<<(QDBusArgument& argument, const Secret& secret)
{
    argument.beginStructure();
    argument << secret.session << secret.parameters << secret.value << secret.contentType;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, Secret& secret)
{
    argument.beginStructure();
    argument >> secret.session >> secret.parameters >> secret.value >> secret.contentType;
    argument.endStructure();
    return argument;
}

using Attributes = QMap<QString, QString>;

}  // namespace nutty::secretservice

Q_DECLARE_METATYPE(nutty::secretservice::Secret)

namespace nutty::secretservice {

Q_LOGGING_CATEGORY(lcSecrets, "nutty.secrets")

const QString kService = QStringLiteral("org.freedesktop.secrets");
const QString kServicePath = QStringLiteral("/org/freedesktop/secrets");
const QString kServiceInterface = QStringLiteral("org.freedesktop.Secret.Service");
const QString kCollectionInterface = QStringLiteral("org.freedesktop.Secret.Collection");
const QString kItemInterface = QStringLiteral("org.freedesktop.Secret.Item");
const QString kPromptInterface = QStringLiteral("org.freedesktop.Secret.Prompt");
const QString kSessionInterface = QStringLiteral("org.freedesktop.Secret.Session");
const QString kNoPrompt = QStringLiteral("/");

// Plain calls are quick; prompts wait for the user to type a password.
constexpr int kCallTimeoutMs = 10000;
constexpr int kPromptTimeoutMs = 120000;

// Waits for a Prompt's Completed signal. QtDBus delivers signals to slots,
// so this has to be a QObject.
class PromptWaiter : public QObject
{
    Q_OBJECT

public:
    bool finished = false;
    bool dismissed = false;
    QVariant result;

public slots:
    void onCompleted(bool wasDismissed, const QDBusVariant& value)
    {
        finished = true;
        dismissed = wasDismissed;
        result = value.variant();
        emit done();
    }

signals:
    void done();
};

QDBusObjectPath toPath(const QVariant& value)
{
    return qdbus_cast<QDBusObjectPath>(value);
}

QList<QDBusObjectPath> toPathList(const QVariant& value)
{
    return qdbus_cast<QList<QDBusObjectPath>>(value);
}

class SecretServiceStore : public SecretStore
{
public:
    SecretServiceStore()
        : m_bus(QDBusConnection::sessionBus())
    {
    }

    ~SecretServiceStore() override
    {
        if (!m_session.isEmpty())
            call(m_session, kSessionInterface, QStringLiteral("Close"), {});
    }

    bool isSecure() const override { return true; }
    QString backendName() const override
    {
        return QCoreApplication::translate("nutty::SecretStore", "Secret Service");
    }

    // Opens the transfer session; false when no Secret Service answers.
    bool open(QString* error = nullptr)
    {
        const auto reply = call(kServicePath, kServiceInterface, QStringLiteral("OpenSession"),
            { QStringLiteral("plain"), QVariant::fromValue(QDBusVariant(QString())) });
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2) {
            if (error)
                *error = reply.errorMessage();
            return false;
        }
        m_session = toPath(reply.arguments().at(1)).path();
        return !m_session.isEmpty();
    }

    std::expected<std::optional<QString>, QString> read(const QString& key) override
    {
        auto items = findItems(key);
        if (!items)
            return std::unexpected(items.error());
        if (items->isEmpty())
            return std::optional<QString>();

        const auto session = ensureSession();
        if (!session)
            return std::unexpected(session.error());
        const auto reply = call(items->first().path(), kItemInterface, QStringLiteral("GetSecret"),
            { QVariant::fromValue(QDBusObjectPath(*session)) });
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
            return std::unexpected(errorFrom(reply));
        const auto secret = qdbus_cast<Secret>(reply.arguments().first());
        return std::optional<QString>(QString::fromUtf8(secret.value));
    }

    std::expected<void, QString> write(const QString& key, const QString& value) override
    {
        const auto collection = defaultCollection();
        if (!collection)
            return std::unexpected(collection.error());
        if (auto unlocked = unlock({ QDBusObjectPath(*collection) }); !unlocked)
            return std::unexpected(unlocked.error());
        const auto session = ensureSession();
        if (!session)
            return std::unexpected(session.error());

        const QVariantMap properties {
            { QStringLiteral("org.freedesktop.Secret.Item.Label"),
                QStringLiteral("%1 (%2)").arg(QStringLiteral(NUTTY_APP_NAME), key) },
            { QStringLiteral("org.freedesktop.Secret.Item.Attributes"),
                QVariant::fromValue(attributesFor(key)) },
        };
        const Secret secret { QDBusObjectPath(*session), {}, value.toUtf8(),
            QStringLiteral("text/plain; charset=utf8") };
        const auto reply = call(*collection, kCollectionInterface, QStringLiteral("CreateItem"),
            { properties, QVariant::fromValue(secret), true });
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2)
            return std::unexpected(errorFrom(reply));
        const auto prompt = toPath(reply.arguments().at(1)).path();
        if (prompt != kNoPrompt) {
            const auto result = runPrompt(prompt);
            if (!result)
                return std::unexpected(result.error());
        }
        return {};
    }

    std::expected<void, QString> remove(const QString& key) override
    {
        auto items = findItems(key);
        if (!items)
            return std::unexpected(items.error());
        for (const auto& item : std::as_const(*items)) {
            const auto reply = call(item.path(), kItemInterface, QStringLiteral("Delete"), {});
            if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
                return std::unexpected(errorFrom(reply));
            const auto prompt = toPath(reply.arguments().first()).path();
            if (prompt != kNoPrompt) {
                const auto result = runPrompt(prompt);
                if (!result)
                    return std::unexpected(result.error());
            }
        }
        return {};
    }

private:
    static Attributes attributesFor(const QString& key)
    {
        return { { QStringLiteral("xdg:schema"), QStringLiteral(NUTTY_APP_ID) },
            { QStringLiteral("key"), key } };
    }

    static QString errorFrom(const QDBusMessage& reply)
    {
        if (reply.type() == QDBusMessage::ErrorMessage)
            return reply.errorMessage().isEmpty() ? reply.errorName() : reply.errorMessage();
        return QCoreApplication::translate("nutty::SecretStore", "Unexpected reply from the Secret Service.");
    }

    QDBusMessage call(const QString& path, const QString& interface, const QString& method,
        const QVariantList& arguments, int timeoutMs = kCallTimeoutMs)
    {
        auto message = QDBusMessage::createMethodCall(kService, path, interface, method);
        message.setArguments(arguments);
        return m_bus.call(message, QDBus::Block, timeoutMs);
    }

    // The session can vanish when the keyring daemon restarts; reopen then.
    std::expected<QString, QString> ensureSession()
    {
        if (m_session.isEmpty()) {
            QString error;
            if (!open(&error))
                return std::unexpected(error.isEmpty() ? QCoreApplication::translate("nutty::SecretStore",
                                                             "The Secret Service is not available.")
                                                       : error);
        }
        return m_session;
    }

    // Matching items, unlocking any that are locked.
    std::expected<QList<QDBusObjectPath>, QString> findItems(const QString& key)
    {
        const auto reply = call(kServicePath, kServiceInterface, QStringLiteral("SearchItems"),
            { QVariant::fromValue(attributesFor(key)) });
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2)
            return std::unexpected(errorFrom(reply));
        auto items = toPathList(reply.arguments().at(0));
        const auto locked = toPathList(reply.arguments().at(1));
        if (!locked.isEmpty()) {
            if (auto unlocked = unlock(locked); !unlocked)
                return std::unexpected(unlocked.error());
            items += locked;
        }
        return items;
    }

    std::expected<void, QString> unlock(const QList<QDBusObjectPath>& objects)
    {
        const auto reply = call(
            kServicePath, kServiceInterface, QStringLiteral("Unlock"), { QVariant::fromValue(objects) });
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2)
            return std::unexpected(errorFrom(reply));
        const auto prompt = toPath(reply.arguments().at(1)).path();
        if (prompt == kNoPrompt)
            return {};
        const auto result = runPrompt(prompt);
        if (!result)
            return std::unexpected(result.error());
        return {};
    }

    // The default collection, created (with the daemon's own prompt) if
    // the keyring has none yet.
    std::expected<QString, QString> defaultCollection()
    {
        auto reply = call(
            kServicePath, kServiceInterface, QStringLiteral("ReadAlias"), { QStringLiteral("default") });
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
            return std::unexpected(errorFrom(reply));
        const auto path = toPath(reply.arguments().first()).path();
        if (!path.isEmpty() && path != kNoPrompt)
            return path;

        const QVariantMap properties { { QStringLiteral("org.freedesktop.Secret.Collection.Label"),
            QCoreApplication::translate("nutty::SecretStore", "Login") } };
        reply = call(kServicePath, kServiceInterface, QStringLiteral("CreateCollection"),
            { properties, QStringLiteral("default") });
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2)
            return std::unexpected(errorFrom(reply));
        const auto created = toPath(reply.arguments().at(0)).path();
        if (!created.isEmpty() && created != kNoPrompt)
            return created;
        const auto result = runPrompt(toPath(reply.arguments().at(1)).path());
        if (!result)
            return std::unexpected(result.error());
        const auto prompted = toPath(*result).path();
        if (prompted.isEmpty() || prompted == kNoPrompt)
            return std::unexpected(QCoreApplication::translate(
                "nutty::SecretStore", "The keyring did not create a collection."));
        return prompted;
    }

    // Shows a prompt and waits for it. The result is the prompt's value
    // (e.g. the created collection path).
    std::expected<QVariant, QString> runPrompt(const QString& prompt)
    {
        PromptWaiter waiter;
        if (!m_bus.connect(kService, prompt, kPromptInterface, QStringLiteral("Completed"), &waiter,
                SLOT(onCompleted(bool, QDBusVariant))))
            return std::unexpected(QCoreApplication::translate(
                "nutty::SecretStore", "Could not listen for the keyring prompt."));

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&waiter, &PromptWaiter::done, &loop, &QEventLoop::quit);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

        const auto reply = call(prompt, kPromptInterface, QStringLiteral("Prompt"), { QString() });
        if (reply.type() == QDBusMessage::ErrorMessage) {
            m_bus.disconnect(kService, prompt, kPromptInterface, QStringLiteral("Completed"), &waiter,
                SLOT(onCompleted(bool, QDBusVariant)));
            return std::unexpected(errorFrom(reply));
        }
        if (!waiter.finished) {
            qCDebug(lcSecrets) << "waiting for keyring prompt";
            timer.start(kPromptTimeoutMs);
            loop.exec(QEventLoop::ExcludeUserInputEvents);
        }
        m_bus.disconnect(kService, prompt, kPromptInterface, QStringLiteral("Completed"), &waiter,
            SLOT(onCompleted(bool, QDBusVariant)));

        if (!waiter.finished) {
            call(prompt, kPromptInterface, QStringLiteral("Dismiss"), {});
            return std::unexpected(
                QCoreApplication::translate("nutty::SecretStore", "The keyring prompt timed out."));
        }
        if (waiter.dismissed)
            return std::unexpected(
                QCoreApplication::translate("nutty::SecretStore", "The keyring prompt was dismissed."));
        return waiter.result;
    }

    QDBusConnection m_bus;
    QString m_session;
};

}  // namespace nutty::secretservice

namespace nutty {

std::unique_ptr<SecretStore> createSystemSecretStore()
{
    using namespace secretservice;

    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return nullptr;
    // Don't start a keyring daemon that the desktop doesn't provide.
    auto* busInterface = bus.interface();
    const bool registered = busInterface && busInterface->isServiceRegistered(kService).value();
    const bool activatable = busInterface
        && busInterface->call(QStringLiteral("ListActivatableNames"))
               .arguments()
               .value(0)
               .toStringList()
               .contains(kService);
    if (!registered && !activatable)
        return nullptr;

    static const bool typesRegistered = [] {
        qDBusRegisterMetaType<Secret>();
        qDBusRegisterMetaType<Attributes>();
        return true;
    }();
    Q_UNUSED(typesRegistered);

    auto store = std::make_unique<SecretServiceStore>();
    QString error;
    if (!store->open(&error)) {
        qCInfo(lcSecrets) << "Secret Service unavailable:" << error;
        return nullptr;
    }
    return store;
}

}  // namespace nutty

#include "SecretStore_linux.moc"
