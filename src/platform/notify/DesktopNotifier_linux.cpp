// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/notify/DesktopNotifier.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QPointer>
#include <QSet>

namespace nutty {

namespace {

const QString kService = QStringLiteral("org.freedesktop.Notifications");
const QString kPath = QStringLiteral("/org/freedesktop/Notifications");
const QString kInterface = QStringLiteral("org.freedesktop.Notifications");

// Receives ActionInvoked / NotificationClosed. Lives in the .cpp because
// QtDBus needs real slots and the header must stay free of D-Bus types.
class NotificationListener : public QObject
{
    Q_OBJECT

public:
    explicit NotificationListener(DesktopNotifier* notifier)
        : QObject(notifier)
        , m_notifier(notifier)
    {
        auto bus = QDBusConnection::sessionBus();
        bus.connect(kService, kPath, kInterface, QStringLiteral("ActionInvoked"), this,
            SLOT(onActionInvoked(uint, QString)));
        bus.connect(kService, kPath, kInterface, QStringLiteral("NotificationClosed"), this,
            SLOT(onNotificationClosed(uint, uint)));
    }

public slots:
    void onActionInvoked(uint id, const QString& actionKey) { m_notifier->handleAction(id, actionKey); }
    void onNotificationClosed(uint id, uint) { m_notifier->handleClosed(id); }

private:
    DesktopNotifier* m_notifier;
};

// Body markup is optional in the spec; escape only for servers that parse it.
QString escapeMarkup(const QString& text)
{
    return text.toHtmlEscaped();
}

uchar urgencyFor(Severity severity)
{
    switch (severity) {
    case Severity::Info:
        return 0;
    case Severity::Warning:
        return 1;
    case Severity::Critical:
        return 2;
    }
    return 1;
}

}  // namespace

struct DesktopNotifier::Private {
    NotificationListener* listener = nullptr;
    QSet<quint32> ids;  // notifications we posted that are still open
    bool bodyMarkup = false;
};

DesktopNotifier::DesktopNotifier(ChannelConfig config, TrayIconProvider tray, QObject* parent)
    : Notifier(std::move(config), parent)
    , d(std::make_unique<Private>())
    , m_tray(std::move(tray))
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    d->listener = new NotificationListener(this);

    QDBusInterface iface(kService, kPath, kInterface, bus);
    auto* watcher = new QDBusPendingCallWatcher(iface.asyncCall(QStringLiteral("GetCapabilities")), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* call) {
        const QDBusPendingReply<QStringList> reply = *call;
        if (reply.isValid())
            d->bodyMarkup = reply.value().contains(QStringLiteral("body-markup"));
        call->deleteLater();
    });
}

DesktopNotifier::~DesktopNotifier() = default;

bool DesktopNotifier::sendNative(const Event& event)
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;

    const bool cancellable = event.type == EventType::ActionScheduled
        && event.extra.value(QStringLiteral("cancellable")).toBool();
    QStringList actions { QStringLiteral("default"), tr("Open nutty") };
    if (cancellable)
        actions << QStringLiteral("cancel") << tr("Cancel");

    const QVariantMap hints {
        { QStringLiteral("urgency"), QVariant::fromValue(urgencyFor(event.severity)) },
        { QStringLiteral("desktop-entry"), QStringLiteral(NUTTY_APP_ID) },
        { QStringLiteral("category"), QStringLiteral("device") },
    };
    // A pending shutdown stays on screen until it happens or is cancelled.
    const int timeout = cancellable ? 0 : -1;
    const auto body = d->bodyMarkup ? escapeMarkup(event.message) : event.message;

    QDBusInterface iface(kService, kPath, kInterface, bus);
    const auto pending = iface.asyncCall(QStringLiteral("Notify"), QStringLiteral("nutty"), uint(0),
        QStringLiteral(NUTTY_APP_ID), event.title, body, actions, hints, timeout);
    auto* watcher = new QDBusPendingCallWatcher(pending, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, event](QDBusPendingCallWatcher* call) {
        const QDBusPendingReply<uint> reply = *call;
        call->deleteLater();
        if (reply.isValid()) {
            d->ids.insert(reply.value());
            deliveredNatively(true, event, {});
        } else {
            deliveredNatively(false, event, reply.error().message());
        }
    });
    return true;
}

void DesktopNotifier::handleAction(quint32 notificationId, const QString& actionKey)
{
    if (!d->ids.contains(notificationId))
        return;
    emit actionInvoked(actionKey == u"default" ? QStringLiteral("open") : actionKey);
}

void DesktopNotifier::handleClosed(quint32 notificationId)
{
    d->ids.remove(notificationId);
}

}  // namespace nutty

#include "DesktopNotifier_linux.moc"
