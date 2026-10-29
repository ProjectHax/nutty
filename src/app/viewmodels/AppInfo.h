// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

namespace nutty {

// Facts about this build for the About page, as the singleton `AppInfo`.
class AppInfo : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString copyright READ copyright CONSTANT)
    Q_PROPERTY(QString license READ license CONSTANT)
    Q_PROPERTY(QUrl homepage READ homepage CONSTANT)
    Q_PROPERTY(QUrl issues READ issues CONSTANT)
    Q_PROPERTY(QUrl licenseUrl READ licenseUrl CONSTANT)
    Q_PROPERTY(QString qtVersion READ qtVersion CONSTANT)
    Q_PROPERTY(QString platform READ platform CONSTANT)
    Q_PROPERTY(QString dataDir READ dataDir CONSTANT)
    Q_PROPERTY(QUrl icon READ icon CONSTANT)

public:
    explicit AppInfo(QObject* parent = nullptr);
    static AppInfo* create(QQmlEngine*, QJSEngine*);

    QString name() const { return QStringLiteral("nutty"); }
    QString version() const { return QStringLiteral(NUTTY_VERSION); }
    QString copyright() const;
    QString license() const { return QStringLiteral("GPL-3.0-only"); }
    QUrl homepage() const { return QUrl(QStringLiteral(NUTTY_HOMEPAGE)); }
    QUrl issues() const { return QUrl(QStringLiteral(NUTTY_HOMEPAGE "/issues")); }
    QUrl licenseUrl() const { return QUrl(QStringLiteral("https://www.gnu.org/licenses/gpl-3.0.html")); }
    QString qtVersion() const;
    QString platform() const;
    QString dataDir() const;
    QUrl icon() const { return QUrl(QStringLiteral("qrc:/nutty/appicon/nutty.svg")); }

    Q_INVOKABLE bool openUrl(const QUrl& url) const;
    // Asks where to save a file (the system dialog); empty when cancelled.
    Q_INVOKABLE QUrl saveFileUrl(
        const QString& title, const QString& suggestedName, const QString& filter) const;
    Q_INVOKABLE bool openFolder(const QString& path) const;
};

}  // namespace nutty
