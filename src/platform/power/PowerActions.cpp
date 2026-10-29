// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/power/PowerActions.h"

#include <QCoreApplication>
#include <QFile>
#include <QProcess>

// The parts that are the same everywhere; the OS-specific halves live in
// PowerActions_{linux,win,mac}.

namespace nutty::PowerActions {

bool isSandboxed()
{
#if defined(Q_OS_LINUX)
    return QFile::exists(QStringLiteral("/.flatpak-info"));
#else
    return false;
#endif
}

std::expected<void, QString> runCommand(const QString& command)
{
    if (command.trimmed().isEmpty())
        return std::unexpected(QCoreApplication::translate("nutty::PowerActions", "No command is set."));

#if defined(Q_OS_WIN)
    const QString program = QStringLiteral("cmd.exe");
    const QStringList arguments { QStringLiteral("/C"), command };
#else
    const QString program = QStringLiteral("/bin/sh");
    const QStringList arguments { QStringLiteral("-c"), command };
#endif
    if (!QProcess::startDetached(program, arguments))
        return std::unexpected(
            QCoreApplication::translate("nutty::PowerActions", "The command could not be started."));
    return {};
}

}  // namespace nutty::PowerActions
