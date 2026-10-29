// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "app/viewmodels/ChannelEditor.h"

#include "app/viewmodels/AppContext.h"
#include "app/viewmodels/FieldSpecs.h"
#include "app/viewmodels/Labels.h"
#include "core/notify/Dispatcher.h"

#include <algorithm>

namespace nutty {

ChannelEditor::ChannelEditor(QObject* parent)
    : ChannelEditor(AppContext::instance(), parent)
{
}

ChannelEditor::ChannelEditor(AppContext* context, QObject* parent)
    : QObject(parent)
    , m_context(context)
{
    if (m_context && m_context->dispatcher()) {
        connect(m_context->dispatcher(), &Dispatcher::testFinished, this,
            [this](const QUuid& channelId, bool ok, const QString& error) {
                if (!m_testing || channelId != m_testId)
                    return;
                m_testing = false;
                m_testDone = true;
                m_testOk = ok;
                m_testResult = ok ? tr("Sent. Check that it arrived.") : error;
                emit testChanged();
            });
    }
    reset(QStringLiteral("Desktop"));
}

ChannelEditor::~ChannelEditor() = default;

QString ChannelEditor::channelId() const
{
    return idString(m_channel.id);
}

QString ChannelEditor::type() const
{
    return enumKey(m_channel.type);
}

void ChannelEditor::setType(const QString& type)
{
    const ChannelType t = enumFromKey(type, m_channel.type);
    if (t == m_channel.type)
        return;
    const QString oldName = labels::channelTypeName(m_channel.type);
    m_channel.type = t;
    m_channel.settings = fields::channelDefaults(t);
    m_channel.secrets.clear();
    if (m_channel.name.isEmpty() || m_channel.name == oldName)
        m_channel.name = labels::channelTypeName(t);
    emit typeChanged();
    emit valuesChanged();
    markEdited();
}

QString ChannelEditor::typeName() const
{
    return labels::channelTypeName(m_channel.type);
}

QVariantList ChannelEditor::fields() const
{
    return fields::forChannel(m_channel.type);
}

QVariantMap ChannelEditor::values() const
{
    QVariantMap map = m_channel.settings;
    for (auto it = m_channel.secrets.cbegin(); it != m_channel.secrets.cend(); ++it)
        map.insert(it.key(), it.value());
    return map;
}

void ChannelEditor::setName(const QString& name)
{
    if (name == m_channel.name)
        return;
    m_channel.name = name;
    markEdited();
}

void ChannelEditor::setEnabled(bool enabled)
{
    if (enabled == m_channel.enabled)
        return;
    m_channel.enabled = enabled;
    markEdited();
}

QStringList ChannelEditor::events() const
{
    QStringList keys;
    for (const EventType e : m_channel.events)
        keys << enumKey(e);
    keys.sort();
    return keys;
}

void ChannelEditor::setEvents(const QStringList& keys)
{
    QSet<EventType> set;
    for (const QString& k : keys)
        set.insert(enumFromKey(k, EventType::Test));
    set.insert(EventType::Test);  // tests always get through
    if (set == m_channel.events)
        return;
    m_channel.events = set;
    markEdited();
}

bool ChannelEditor::hasEvent(const QString& key) const
{
    return m_channel.events.contains(enumFromKey(key, EventType::Test));
}

void ChannelEditor::setEvent(const QString& key, bool on)
{
    const EventType e = enumFromKey(key, EventType::Test);
    if (e == EventType::Test || m_channel.events.contains(e) == on)
        return;
    if (on)
        m_channel.events.insert(e);
    else
        m_channel.events.remove(e);
    markEdited();
}

void ChannelEditor::emitAll()
{
    emit loaded();
    emit typeChanged();
    emit valuesChanged();
    emit edited();
    emit testChanged();
}

bool ChannelEditor::load(const QString& id)
{
    const ChannelConfig* channel = m_context ? m_context->config().channel(parseId(id)) : nullptr;
    if (!channel) {
        reset(QStringLiteral("Desktop"));
        return false;
    }
    m_channel = *channel;
    m_isNew = false;
    m_dirty = false;
    m_testing = false;
    m_testDone = false;
    m_testResult.clear();
    setError({});
    emitAll();
    return true;
}

void ChannelEditor::reset(const QString& type)
{
    m_channel = ChannelConfig {};
    m_channel.id = QUuid::createUuid();
    m_channel.type = enumFromKey(type, ChannelType::Desktop);
    m_channel.name = labels::channelTypeName(m_channel.type);
    m_channel.events = defaultChannelEvents(m_channel.type);
    m_channel.settings = fields::channelDefaults(m_channel.type);
    m_isNew = true;
    m_dirty = false;
    m_testing = false;
    m_testDone = false;
    m_testResult.clear();
    setError({});
    emitAll();
}

QVariant ChannelEditor::value(const QString& key) const
{
    if (m_channel.secrets.contains(key))
        return m_channel.secrets.value(key);
    return m_channel.settings.value(key);
}

void ChannelEditor::setValue(const QString& key, const QVariant& value)
{
    if (key.isEmpty())
        return;
    QString kind;
    for (const QVariant& f : fields::forChannel(m_channel.type)) {
        const QVariantMap m = f.toMap();
        if (m.value(QStringLiteral("key")).toString() == key)
            kind = m.value(QStringLiteral("kind")).toString();
    }
    QVariant v = value;
    if (kind == u"number")
        v = v.toString().trimmed().isEmpty() ? QVariant() : QVariant(v.toInt());
    else if (kind == u"bool")
        v = v.toBool();
    else
        v = v.toString();
    const bool secret = secretKeys(m_channel.type).contains(key);
    QVariantMap& target = secret ? m_channel.secrets : m_channel.settings;
    if (target.value(key) == v)
        return;
    if (v.isNull() || (v.typeId() == QMetaType::QString && v.toString().isEmpty()))
        target.remove(key);
    else
        target.insert(key, v);
    emit valuesChanged();
    markEdited();
}

bool ChannelEditor::fieldVisible(const QVariantMap& field) const
{
    const QVariantMap condition = field.value(QStringLiteral("showIf")).toMap();
    for (auto it = condition.cbegin(); it != condition.cend(); ++it) {
        if (value(it.key()).toString() != it.value().toString())
            return false;
    }
    return true;
}

ChannelConfig ChannelEditor::current() const
{
    ChannelConfig c = m_channel;
    c.name = c.name.trimmed();
    if (c.name.isEmpty())
        c.name = labels::channelTypeName(c.type);
    return c;
}

QString ChannelEditor::validate() const
{
    for (const QVariant& f : fields::forChannel(m_channel.type)) {
        const QVariantMap m = f.toMap();
        if (!m.value(QStringLiteral("required")).toBool() || !fieldVisible(m))
            continue;
        if (value(m.value(QStringLiteral("key")).toString()).toString().trimmed().isEmpty())
            return tr("%1 is required.").arg(m.value(QStringLiteral("label")).toString());
    }
    return {};
}

void ChannelEditor::sendTest()
{
    Dispatcher* dispatcher = m_context ? m_context->dispatcher() : nullptr;
    m_testDone = false;
    m_testResult.clear();
    if (const QString problem = validate(); !problem.isEmpty() || !dispatcher) {
        m_testDone = true;
        m_testOk = false;
        m_testResult = dispatcher ? problem : tr("Notifications are not available.");
        emit testChanged();
        return;
    }
    ChannelConfig channel = current();
    channel.enabled = true;
    m_testId = channel.id;
    m_testing = true;
    emit testChanged();
    dispatcher->sendTest(channel);
}

bool ChannelEditor::save()
{
    if (!m_context)
        return false;
    if (const QString problem = validate(); !problem.isEmpty()) {
        setError(problem);
        return false;
    }
    const ChannelConfig channel = current();
    AppConfig config = m_context->config();
    const auto it = std::ranges::find(config.channels, channel.id, &ChannelConfig::id);
    if (it == config.channels.end())
        config.channels.append(channel);
    else
        *it = channel;
    const bool ok = m_context->updateConfig(config);
    setError(ok ? QString() : m_context->lastError());
    m_channel = channel;
    m_isNew = false;
    m_dirty = false;
    emit loaded();
    emit edited();
    emit saved(idString(channel.id));
    return ok;
}

bool ChannelEditor::remove()
{
    if (!m_context || m_isNew)
        return false;
    AppConfig config = m_context->config();
    const QUuid id = m_channel.id;
    config.channels.removeIf([&](const ChannelConfig& c) { return c.id == id; });
    for (DeviceConfig& d : config.devices)
        d.channels.removeAll(id);
    const bool ok = m_context->updateConfig(config);
    emit removed(idString(id));
    return ok;
}

void ChannelEditor::markEdited()
{
    m_dirty = true;
    emit edited();
}

void ChannelEditor::setError(const QString& error)
{
    if (error == m_error)
        return;
    m_error = error;
    emit errorChanged();
}

}  // namespace nutty
