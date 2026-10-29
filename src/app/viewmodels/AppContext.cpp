// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "app/viewmodels/AppContext.h"

#include "core/engine/Monitor.h"
#include "core/notify/Dispatcher.h"
#include "core/storage/ConfigStore.h"
#include "core/storage/HistoryStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QUrlQuery>

namespace nutty {

namespace {
QList<QPointer<AppContext>>& contexts()
{
    static QList<QPointer<AppContext>> list;
    return list;
}
}  // namespace

AppContext::AppContext(const Services& services, QObject* parent)
    : QObject(parent)
    , m_services(services)
{
    Q_ASSERT(m_services.monitor);
    m_config = m_services.monitor->config();
    contexts().append(this);
}

AppContext::~AppContext()
{
    contexts().removeAll(this);
}

AppContext* AppContext::instance()
{
    auto& list = contexts();
    list.removeAll(nullptr);
    return list.isEmpty() ? nullptr : list.constLast().data();
}

void AppContext::setConfig(const AppConfig& config)
{
    apply(config);
}

bool AppContext::updateConfig(const AppConfig& config)
{
    bool ok = true;
    QString error;
    if (auto* store = m_services.configStore) {
        ok = store->save(config);
        error = store->lastError();
        if (!ok && error.isEmpty())
            error = tr("Could not save the settings to %1.").arg(QDir::toNativeSeparators(store->path()));
    }
    if (error != m_lastError) {
        m_lastError = error;
        emit lastErrorChanged();
    }
    apply(config);
    return ok;
}

void AppContext::apply(const AppConfig& config)
{
    const AppConfig previous = m_config;
    m_config = config;
    m_services.monitor->applyConfig(m_config);
    if (m_services.dispatcher)
        m_services.dispatcher->applyConfig(m_config);
    emit configChanged(previous);
}

bool AppContext::removeDevice(const QUuid& id)
{
    AppConfig config = m_config;
    QString image;
    const auto removed = config.devices.removeIf([&](const DeviceConfig& d) {
        if (d.id != id)
            return false;
        image = d.customImage;
        return true;
    });
    if (removed == 0)
        return false;
    config.rules.removeIf([&](const Rule& r) { return r.scope == RuleScope::Device && r.target == id; });
    for (auto& group : config.groups)
        group.members.removeAll(id);
    const bool ok = updateConfig(config);
    if (!image.isEmpty())
        QFile::remove(imagesDir() + u'/' + image);
    if (m_services.history)
        m_services.history->forgetDevice(id);
    return ok;
}

QString AppContext::imagesDir() const
{
    return m_services.dataDir.isEmpty() ? QString() : m_services.dataDir + QStringLiteral("/images");
}

QUrl AppContext::imageUrl(const QString& fileName) const
{
    if (fileName.isEmpty() || m_services.dataDir.isEmpty())
        return {};
    const QFileInfo info(imagesDir() + u'/' + fileName);
    if (!info.exists())
        return {};
    QUrl url = QUrl::fromLocalFile(info.absoluteFilePath());
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("v"), QString::number(info.lastModified().toMSecsSinceEpoch()));
    url.setQuery(query);
    return url;
}

QString idString(const QUuid& id)
{
    return id.isNull() ? QString() : id.toString(QUuid::WithoutBraces);
}

QUuid parseId(const QString& id)
{
    return QUuid::fromString(id);
}

QString displayPath(const QString& path)
{
    const QString clean = QDir::cleanPath(path);
    const QString home = QDir::cleanPath(QDir::homePath());
    if (!home.isEmpty() && home != u"/" && (clean == home || clean.startsWith(home + u'/')))
        return QDir::toNativeSeparators(u'~' + clean.mid(home.size()));
    return QDir::toNativeSeparators(clean);
}

QString expandPath(const QString& path)
{
    const QString clean = QDir::fromNativeSeparators(path);
    if (clean == u"~" || clean.startsWith(u"~/"))
        return QDir::homePath() + clean.mid(1);
    return clean;
}

}  // namespace nutty
