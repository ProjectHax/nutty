// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "app/viewmodels/AppInfo.h"

#include "app/viewmodels/AppContext.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSysInfo>

namespace nutty {

AppInfo::AppInfo(QObject* parent)
    : QObject(parent)
{
}

AppInfo* AppInfo::create(QQmlEngine*, QJSEngine*)
{
    return new AppInfo;
}

QString AppInfo::copyright() const
{
    return tr("Copyright © 2026 ProjectHax LLC");
}

QString AppInfo::qtVersion() const
{
    return QString::fromLatin1(qVersion());
}

QString AppInfo::platform() const
{
    return QSysInfo::prettyProductName() + QStringLiteral(" · ") + QSysInfo::currentCpuArchitecture();
}

QString AppInfo::dataDir() const
{
    const AppContext* context = AppContext::instance();
    return context ? displayPath(context->dataDir()) : QString();
}

bool AppInfo::openUrl(const QUrl& url) const
{
    if (url.scheme() != u"https" && url.scheme() != u"http" && url.scheme() != u"mailto")
        return false;
    return QDesktopServices::openUrl(url);
}

QUrl AppInfo::saveFileUrl(const QString& title, const QString& suggestedName, const QString& filter) const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString file = QFileDialog::getSaveFileName(nullptr, title, dir + u'/' + suggestedName, filter);
    return file.isEmpty() ? QUrl() : QUrl::fromLocalFile(file);
}

bool AppInfo::openFolder(const QString& path) const
{
    if (path.isEmpty())
        return false;
    // A file opens the folder that holds it.
    const QFileInfo info(expandPath(path));
    const QString dir = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    if (!QDir(dir).exists())
        return false;
    return QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

}  // namespace nutty
