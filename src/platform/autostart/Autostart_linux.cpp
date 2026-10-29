// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/autostart/Autostart.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>

namespace nutty::Autostart {

namespace {

bool inFlatpak()
{
    return QFile::exists(QStringLiteral("/.flatpak-info"));
}

QString autostartFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/autostart/" NUTTY_APP_ID ".desktop");
}

// The portal has no way to read the setting back, so remember what we asked for.
QString flatpakMarker()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + QStringLiteral("/flatpak-autostart");
}

// Quotes one Exec argument per the Desktop Entry spec: reserved characters
// force double quotes, inside which " ` $ \ are backslash-escaped; then the
// string-level escaping doubles every backslash, and % becomes %%.
QString quoteExecArgument(const QString& arg)
{
    static const QString reserved = QStringLiteral(" \t\n\"'\\><~|&;$*?#()`");
    QString out = arg;
    if (std::ranges::any_of(arg, [](QChar c) { return reserved.contains(c); })) {
        out.clear();
        for (const QChar c : arg) {
            if (c == u'"' || c == u'`' || c == u'$' || c == u'\\')
                out += u'\\';
            out += c;
        }
        out = u'"' + out + u'"';
    }
    out.replace(u'\\', QStringLiteral("\\\\"));
    out.replace(u'%', QStringLiteral("%%"));
    return out;
}

QString executablePath()
{
    // Inside an AppImage, applicationFilePath() is a path in the temporary
    // mount; $APPIMAGE is the file the user actually has.
    const auto appImage = qEnvironmentVariable("APPIMAGE");
    if (!appImage.isEmpty() && QFileInfo::exists(appImage))
        return appImage;
    return QCoreApplication::applicationFilePath();
}

std::expected<void, QString> requestBackground(bool enable)
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return std::unexpected(
            QCoreApplication::translate("nutty::Autostart", "The session bus is not available."));

    QDBusInterface portal(QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.Background"), bus);
    const QVariantMap options {
        { QStringLiteral("handle_token"),
            QStringLiteral("nutty_autostart_%1").arg(QRandomGenerator::global()->generate()) },
        { QStringLiteral("reason"),
            QCoreApplication::translate(
                "nutty::Autostart", "Start nutty at login so it keeps watching your power.") },
        { QStringLiteral("autostart"), enable },
        { QStringLiteral("commandline"),
            QStringList { QStringLiteral("nutty"), QStringLiteral("--hidden") } },
        { QStringLiteral("dbus-activatable"), false },
    };
    const auto reply = portal.call(QStringLiteral("RequestBackground"), QString(), options);
    if (reply.type() == QDBusMessage::ErrorMessage)
        return std::unexpected(reply.errorMessage());
    return {};
}

}  // namespace

bool isSupported()
{
    return true;
}

bool isEnabled()
{
    if (inFlatpak())
        return QFile::exists(flatpakMarker());
    return QFile::exists(autostartFile());
}

std::expected<void, QString> setEnabled(bool enabled)
{
    if (inFlatpak()) {
        if (auto result = requestBackground(enabled); !result)
            return result;
        if (enabled) {
            QDir().mkpath(QFileInfo(flatpakMarker()).absolutePath());
            QFile marker(flatpakMarker());
            if (marker.open(QIODevice::WriteOnly))
                marker.close();
        } else {
            QFile::remove(flatpakMarker());
        }
        return {};
    }

    const auto path = autostartFile();
    if (!enabled) {
        if (QFile::exists(path) && !QFile::remove(path))
            return std::unexpected(
                QCoreApplication::translate("nutty::Autostart", "Could not remove %1.").arg(path));
        return {};
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return std::unexpected(
            QCoreApplication::translate("nutty::Autostart", "Could not write %1.").arg(path));
    const QString entry = QStringLiteral("[Desktop Entry]\n"
                                         "Type=Application\n"
                                         "Name=nutty\n"
                                         "Comment=UPS and battery monitor\n"
                                         "Exec=%1 --hidden\n"
                                         "Icon=" NUTTY_APP_ID "\n"
                                         "Terminal=false\n"
                                         "X-GNOME-Autostart-enabled=true\n")
                              .arg(quoteExecArgument(executablePath()));
    file.write(entry.toUtf8());
    if (!file.commit())
        return std::unexpected(
            QCoreApplication::translate("nutty::Autostart", "Could not write %1.").arg(path));
    return {};
}

}  // namespace nutty::Autostart
