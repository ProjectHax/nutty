// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "app/viewmodels/Catalog.h"

#include "app/viewmodels/AppContext.h"
#include "app/viewmodels/DeviceInfo.h"
#include "app/viewmodels/Labels.h"
#include "core/engine/EventText.h"
#include "platform/power/PowerActions.h"

#include <algorithm>

namespace nutty {

Catalog::Catalog(AppContext* context, QObject* parent)
    : QObject(parent)
    , m_context(context ? context : AppContext::instance())
{
    if (m_context) {
        connect(m_context, &AppContext::configChanged, this, &Catalog::changed);
        connect(m_context, &AppContext::lastErrorChanged, this, &Catalog::lastErrorChanged);
    }
}

Catalog* Catalog::create(QQmlEngine*, QJSEngine*)
{
    return new Catalog(AppContext::instance());
}

QString Catalog::lastError() const
{
    return m_context ? m_context->lastError() : QString();
}

QVariantList Catalog::devices() const
{
    QVariantList list;
    if (!m_context)
        return list;
    for (const DeviceConfig& d : m_context->config().devices) {
        list.append(QVariantMap {
            { QStringLiteral("id"), idString(d.id) },
            { QStringLiteral("name"), text::deviceName(d) },
            { QStringLiteral("type"), enumKey(d.type) },
            { QStringLiteral("typeName"), deviceinfo::typeName(d.type) },
            { QStringLiteral("icon"), labels::sourceIcon(d.type) },
            { QStringLiteral("enabled"), d.enabled },
        });
    }
    return list;
}

QVariantList Catalog::channels() const
{
    QVariantList list;
    if (!m_context)
        return list;
    for (const ChannelConfig& c : m_context->config().channels) {
        QString description;
        if (c.type == ChannelType::Ntfy)
            description = c.settings.value(QString::fromLatin1(keys::NtfyTopic)).toString();
        else if (c.type == ChannelType::Email)
            description = c.settings.value(QString::fromLatin1(keys::EmailTo)).toString();
        else if (c.type == ChannelType::Webhook)
            description = QUrl(c.settings.value(QString::fromLatin1(keys::WebhookUrl)).toString()).host();
        list.append(QVariantMap {
            { QStringLiteral("id"), idString(c.id) },
            { QStringLiteral("name"), c.name.isEmpty() ? labels::channelTypeName(c.type) : c.name },
            { QStringLiteral("type"), enumKey(c.type) },
            { QStringLiteral("typeName"), labels::channelTypeName(c.type) },
            { QStringLiteral("icon"), labels::channelIcon(c.type) },
            { QStringLiteral("enabled"), c.enabled },
            // Test notifications always get through; they aren't a choice.
            { QStringLiteral("eventCount"),
                static_cast<int>(c.events.size() - (c.events.contains(EventType::Test) ? 1 : 0)) },
            { QStringLiteral("description"), description },
        });
    }
    return list;
}

QVariantList Catalog::rules() const
{
    QVariantList list;
    if (!m_context)
        return list;
    const AppConfig& config = m_context->config();
    for (const Rule& r : config.rules) {
        QString targetName;
        if (r.scope == RuleScope::Device) {
            if (const DeviceConfig* d = config.device(r.target))
                targetName = text::deviceName(*d);
        } else if (const DeviceGroup* g = config.group(r.target)) {
            targetName = g->name;
        }
        const QString condition = labels::conditionText(r.condition, r.threshold);
        QString description;
        if (r.condition == ConditionType::ForcedShutdown)
            description = tr("%1 right away").arg(text::actionVerb(r.action));
        else if (r.action == ActionType::Notify)
            description = tr("Notify");
        else
            description = tr("%1 after a %2 countdown")
                              .arg(text::actionVerb(r.action), text::duration(r.countdownSec));
        const PowerCapability cap = PowerActions::capability(r.action);
        list.append(QVariantMap {
            { QStringLiteral("id"), idString(r.id) },
            { QStringLiteral("name"), r.name },
            { QStringLiteral("enabled"), r.enabled },
            { QStringLiteral("scope"), enumKey(r.scope) },
            { QStringLiteral("target"), idString(r.target) },
            { QStringLiteral("targetName"), targetName.isEmpty() ? tr("(missing)") : targetName },
            { QStringLiteral("condition"), enumKey(r.condition) },
            { QStringLiteral("threshold"), r.threshold },
            { QStringLiteral("conditionText"), condition },
            { QStringLiteral("action"), enumKey(r.action) },
            { QStringLiteral("actionName"), text::actionName(r.action) },
            { QStringLiteral("countdownSec"), r.countdownSec },
            { QStringLiteral("description"), description },
            { QStringLiteral("available"), cap.available },
            { QStringLiteral("caveat"), cap.reason },
        });
    }
    return list;
}

QVariantList Catalog::groups() const
{
    QVariantList list;
    if (!m_context)
        return list;
    const AppConfig& config = m_context->config();
    for (const DeviceGroup& g : config.groups) {
        QStringList ids;
        QStringList names;
        for (const QUuid& m : g.members) {
            ids << idString(m);
            if (const DeviceConfig* d = config.device(m))
                names << text::deviceName(*d);
        }
        list.append(QVariantMap {
            { QStringLiteral("id"), idString(g.id) },
            { QStringLiteral("name"), g.name },
            { QStringLiteral("members"), ids },
            { QStringLiteral("memberNames"), names.join(QStringLiteral(", ")) },
        });
    }
    return list;
}

QVariantList Catalog::sourceTypes() const
{
    return labels::sourceTypes();
}
QVariantList Catalog::channelTypes() const
{
    return labels::channelTypes();
}
QVariantList Catalog::eventTypes() const
{
    return labels::eventTypes();
}
QVariantList Catalog::conditions() const
{
    return labels::conditions();
}
QVariantList Catalog::actionTypes() const
{
    return labels::actionTypes();
}
QVariantList Catalog::scopes() const
{
    return labels::scopes();
}
QVariantList Catalog::deviceKinds() const
{
    return labels::deviceKinds();
}

QString Catalog::deviceName(const QString& id) const
{
    if (!m_context)
        return {};
    const DeviceConfig* d = m_context->config().device(parseId(id));
    return d ? text::deviceName(*d) : QString();
}

namespace {
template <typename T> T* findIn(QList<T>& list, const QUuid& id)
{
    const auto it = std::ranges::find(list, id, &T::id);
    return it == list.end() ? nullptr : &*it;
}
}  // namespace

bool Catalog::setDeviceEnabled(const QString& id, bool enabled)
{
    if (!m_context)
        return false;
    AppConfig config = m_context->config();
    DeviceConfig* d = findIn(config.devices, parseId(id));
    if (!d || d->enabled == enabled)
        return d != nullptr;
    d->enabled = enabled;
    return m_context->updateConfig(config);
}

bool Catalog::removeDevice(const QString& id)
{
    return m_context && m_context->removeDevice(parseId(id));
}

bool Catalog::moveDevice(const QString& id, int delta)
{
    if (!m_context)
        return false;
    AppConfig config = m_context->config();
    const auto it = std::ranges::find(config.devices, parseId(id), &DeviceConfig::id);
    if (it == config.devices.end())
        return false;
    const int from = static_cast<int>(it - config.devices.begin());
    const int to = qBound(0, from + delta, static_cast<int>(config.devices.size()) - 1);
    if (from == to)
        return true;
    config.devices.move(from, to);
    return m_context->updateConfig(config);
}

bool Catalog::setChannelEnabled(const QString& id, bool enabled)
{
    if (!m_context)
        return false;
    AppConfig config = m_context->config();
    ChannelConfig* c = findIn(config.channels, parseId(id));
    if (!c || c->enabled == enabled)
        return c != nullptr;
    c->enabled = enabled;
    return m_context->updateConfig(config);
}

bool Catalog::removeChannel(const QString& id)
{
    if (!m_context)
        return false;
    AppConfig config = m_context->config();
    const QUuid uuid = parseId(id);
    if (config.channels.removeIf([&](const ChannelConfig& c) { return c.id == uuid; }) == 0)
        return false;
    for (DeviceConfig& d : config.devices)
        d.channels.removeAll(uuid);
    return m_context->updateConfig(config);
}

bool Catalog::setRuleEnabled(const QString& id, bool enabled)
{
    if (!m_context)
        return false;
    AppConfig config = m_context->config();
    Rule* r = findIn(config.rules, parseId(id));
    if (!r || r->enabled == enabled)
        return r != nullptr;
    r->enabled = enabled;
    return m_context->updateConfig(config);
}

bool Catalog::removeRule(const QString& id)
{
    if (!m_context)
        return false;
    AppConfig config = m_context->config();
    const QUuid uuid = parseId(id);
    if (config.rules.removeIf([&](const Rule& r) { return r.id == uuid; }) == 0)
        return false;
    return m_context->updateConfig(config);
}

QString Catalog::saveGroup(const QString& id, const QString& name, const QStringList& members)
{
    if (!m_context)
        return {};
    AppConfig config = m_context->config();
    DeviceGroup* group = findIn(config.groups, parseId(id));
    if (!group) {
        config.groups.append(DeviceGroup { QUuid::createUuid(), {}, {} });
        group = &config.groups.last();
    }
    group->name = name.trimmed().isEmpty() ? tr("Group %1").arg(config.groups.size()) : name.trimmed();
    group->members.clear();
    for (const QString& m : members) {
        const QUuid device = parseId(m);
        if (config.device(device) && !group->members.contains(device))
            group->members.append(device);
    }
    const QString saved = idString(group->id);
    m_context->updateConfig(config);
    return saved;
}

bool Catalog::removeGroup(const QString& id)
{
    if (!m_context)
        return false;
    AppConfig config = m_context->config();
    const QUuid uuid = parseId(id);
    if (config.groups.removeIf([&](const DeviceGroup& g) { return g.id == uuid; }) == 0)
        return false;
    config.rules.removeIf([&](const Rule& r) { return r.scope != RuleScope::Device && r.target == uuid; });
    return m_context->updateConfig(config);
}

}  // namespace nutty
