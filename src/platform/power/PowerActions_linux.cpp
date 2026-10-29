// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/power/PowerActions.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>

namespace nutty::PowerActions {

namespace {

constexpr int kDbusTimeoutMs = 5000;

struct LogindMethods {
    const char* can;
    const char* run;
};

std::optional<LogindMethods> methodsFor(ActionType action)
{
    switch (action) {
    case ActionType::Sleep:
        return LogindMethods { "CanSuspend", "Suspend" };
    case ActionType::Hibernate:
        return LogindMethods { "CanHibernate", "Hibernate" };
    case ActionType::Shutdown:
        return LogindMethods { "CanPowerOff", "PowerOff" };
    case ActionType::Notify:
    case ActionType::Command:
        break;
    }
    return std::nullopt;
}

std::unique_ptr<QDBusInterface> logind()
{
    auto bus = QDBusConnection::systemBus();
    if (!bus.isConnected())
        return nullptr;
    auto iface = std::make_unique<QDBusInterface>(QStringLiteral("org.freedesktop.login1"),
        QStringLiteral("/org/freedesktop/login1"), QStringLiteral("org.freedesktop.login1.Manager"), bus);
    iface->setTimeout(kDbusTimeoutMs);
    return iface;
}

}  // namespace

PowerCapability capability(ActionType action)
{
    if (action == ActionType::Notify)
        return { true, {} };
    if (action == ActionType::Command) {
        if (isSandboxed())
            return { true,
                QCoreApplication::translate(
                    "nutty::PowerActions", "Commands run inside the Flatpak sandbox.") };
        return { true, {} };
    }

    const auto methods = methodsFor(action);
    auto iface = logind();
    if (!methods || !iface)
        return { false,
            QCoreApplication::translate("nutty::PowerActions", "systemd-logind is not reachable.") };

    const QDBusReply<QString> reply = iface->call(QString::fromLatin1(methods->can));
    if (!reply.isValid())
        return { false,
            QCoreApplication::translate("nutty::PowerActions", "systemd-logind is not reachable.") };

    const auto answer = reply.value();
    if (answer == u"yes")
        return { true, {} };
    if (answer == u"challenge")
        return { false,
            QCoreApplication::translate("nutty::PowerActions",
                "The system requires a password for this, so nutty can't do it unattended.") };
    if (answer == u"na")
        return { false,
            QCoreApplication::translate("nutty::PowerActions", "This system doesn't support it.") };
    return { false,
        QCoreApplication::translate("nutty::PowerActions", "The system doesn't allow it for this user.") };
}

std::expected<void, QString> execute(ActionType action)
{
    const auto methods = methodsFor(action);
    if (!methods)
        return std::unexpected(QCoreApplication::translate("nutty::PowerActions", "Not a power action."));
    auto iface = logind();
    if (!iface)
        return std::unexpected(
            QCoreApplication::translate("nutty::PowerActions", "systemd-logind is not reachable."));

    // interactive=false: never wait on a polkit password prompt nobody may see.
    const QDBusMessage reply = iface->call(QString::fromLatin1(methods->run), false);
    if (reply.type() == QDBusMessage::ErrorMessage)
        return std::unexpected(reply.errorMessage().isEmpty() ? reply.errorName() : reply.errorMessage());
    return {};
}

std::expected<void, QString> requestShutdownPermission()
{
    return {};
}

}  // namespace nutty::PowerActions
