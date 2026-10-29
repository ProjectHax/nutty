// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/autostart/Autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

namespace nutty::Autostart {

namespace {

constexpr auto kRunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr auto kValueName = "nutty";

QString command()
{
    return QStringLiteral("\"%1\" --hidden")
        .arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
}

}  // namespace

bool isSupported()
{
    return true;
}

bool isEnabled()
{
    QSettings run(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    return run.contains(QString::fromLatin1(kValueName));
}

std::expected<void, QString> setEnabled(bool enabled)
{
    QSettings run(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    if (enabled)
        run.setValue(QString::fromLatin1(kValueName), command());
    else
        run.remove(QString::fromLatin1(kValueName));
    run.sync();
    if (run.status() != QSettings::NoError)
        return std::unexpected(
            QCoreApplication::translate("nutty::Autostart", "Could not update the Windows startup entry."));
    return {};
}

}  // namespace nutty::Autostart
