// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "app/viewmodels/DeviceControls.h"

#include "app/viewmodels/AppContext.h"
#include "app/viewmodels/Labels.h"
#include "core/engine/EventText.h"
#include "core/engine/Monitor.h"

#include <QTimer>

#include <algorithm>

namespace nutty {

namespace {

const char* const kGroups[] = { "outputs", "charging", "battery", "unit", "generator" };

}  // namespace

DeviceControls::DeviceControls(QObject* parent)
    : DeviceControls(AppContext::instance(), parent)
{
}

DeviceControls::DeviceControls(AppContext* context, QObject* parent)
    : QObject(parent)
    , m_context(context)
{
    if (!m_context)
        return;
    Monitor* monitor = m_context->monitor();
    const auto reloadFor = [this](const QUuid& id) {
        if (id == m_id)
            reload();
    };
    connect(monitor, &Monitor::deviceControlsChanged, this, reloadFor);
    connect(monitor, &Monitor::deviceConnectionChanged, this, reloadFor);
    connect(monitor, &Monitor::devicesChanged, this, &DeviceControls::reload);
    connect(monitor, &Monitor::controlFinished, this,
        [this](const QUuid& id, const QString& control, double, ControlResult result, const QString& error) {
            if (id != m_id)
                return;
            QString message = error;
            if (result == ControlResult::Applied)
                message = tr("Done");
            else if (result == ControlResult::Unconfirmed)
                message = tr("Sent, but the device still reports the old value. Check the device itself.");
            setOutcome(control, result, message);
            reload();
            emit finished(control, result == ControlResult::Applied, message);
        });
}

void DeviceControls::setDeviceId(const QString& id)
{
    if (id == m_deviceId)
        return;
    m_deviceId = id;
    m_id = parseId(id);
    m_outcomes.clear();
    emit deviceIdChanged();
    reload();
}

QString DeviceControls::deviceName() const
{
    const DeviceConfig* d = m_context ? m_context->monitor()->device(m_id) : nullptr;
    return d ? text::deviceName(*d) : QString();
}

QString DeviceControls::blockedReason() const
{
    if (!m_context)
        return {};
    const Monitor* monitor = m_context->monitor();
    const DeviceConfig* d = monitor->device(m_id);
    if (!d)
        return {};
    if (!d->enabled || !monitor->source(m_id))
        return tr("Resume the device to change its settings.");
    if (monitor->connectionState(m_id) != ConnectionState::Connected)
        return tr("nutty can't reach the device right now, so its settings can't be changed.");
    return {};
}

void DeviceControls::setOutcome(const QString& id, ControlResult result, const QString& message)
{
    const int generation = ++m_generation;
    m_outcomes.insert(id, { result, message, generation });
    if (result != ControlResult::Applied)
        return;
    QTimer::singleShot(kAppliedShownMs, this, [this, id, generation] {
        const auto it = m_outcomes.constFind(id);
        if (it != m_outcomes.cend() && it->generation == generation) {
            m_outcomes.remove(id);
            reload();
        }
    });
}

void DeviceControls::reload()
{
    const QVariantList previousGroups = m_groups;
    m_controls.clear();
    m_groups.clear();
    m_details.clear();
    m_hasDetails = false;
    if (!m_context || m_id.isNull()) {
        if (previousGroups != m_groups)
            emit groupsChanged();
        emit changed();
        return;
    }
    const Monitor* monitor = m_context->monitor();
    const QList<DeviceControl> list = monitor->controls(m_id);
    const bool blocked = !blockedReason().isEmpty();
    const bool anyPending
        = std::ranges::any_of(list, [](const DeviceControl& c) { return c.pending.has_value(); });

    QHash<QString, QList<int>> byGroup;
    for (const DeviceControl& c : list) {
        bool parentOn = true;
        if (!c.dependsOn.isEmpty()) {
            const auto parent = std::ranges::find(list, c.dependsOn, &DeviceControl::id);
            parentOn = parent != list.end() && parent->value.value_or(0) == 1;
        }
        QVariantList options;
        if (c.kind == ControlKind::Choice) {
            QList<double> values = c.choices;
            // A value set some other way still shows as what it is.
            if (c.value && !values.contains(*c.value)) {
                values << *c.value;
                std::ranges::sort(values);
            }
            for (double v : values)
                options.append(QVariantMap {
                    { QStringLiteral("value"), v },
                    { QStringLiteral("text"), text::controlValue(c, v) },
                });
        }
        const auto outcome = m_outcomes.constFind(c.id);
        const QString group = labels::controlGroup(c.id);
        byGroup[group] << static_cast<int>(m_controls.size());
        m_controls.append(QVariantMap {
            { QStringLiteral("id"), c.id },
            { QStringLiteral("group"), group },
            { QStringLiteral("kind"), enumKey(c.kind) },
            { QStringLiteral("title"), text::controlName(c.id) },
            { QStringLiteral("description"), labels::controlDescription(c.id) },
            { QStringLiteral("icon"), labels::controlIcon(c.id) },
            { QStringLiteral("known"), c.value.has_value() },
            { QStringLiteral("value"), c.value.value_or(c.kind == ControlKind::Level ? c.minimum : 0) },
            { QStringLiteral("valueText"), c.value ? text::controlValue(c, *c.value) : tr("Not reported") },
            { QStringLiteral("minimum"), c.minimum },
            { QStringLiteral("maximum"), c.maximum },
            { QStringLiteral("step"), c.step },
            { QStringLiteral("unit"), c.unit },
            { QStringLiteral("options"), options },
            { QStringLiteral("enabled"), !blocked && !anyPending && c.value.has_value() && parentOn },
            { QStringLiteral("pending"), c.pending.has_value() },
            { QStringLiteral("pendingText"),
                c.pending ? tr("Changing to %1…").arg(text::controlValue(c, *c.pending)) : QString() },
            { QStringLiteral("result"), outcome != m_outcomes.cend() ? enumKey(outcome->result) : QString() },
            { QStringLiteral("message"), outcome != m_outcomes.cend() ? outcome->message : QString() },
        });
    }
    for (const char* key : kGroups) {
        const QString group = QString::fromLatin1(key);
        const auto it = byGroup.constFind(group);
        if (it == byGroup.cend())
            continue;
        QVariantList indexes;
        for (int i : *it)
            indexes << i;
        m_groups.append(QVariantMap {
            { QStringLiteral("key"), group },
            { QStringLiteral("title"), labels::controlGroupName(group) },
            { QStringLiteral("controls"), indexes },
        });
    }

    const DeviceDetails d = monitor->details(m_id);
    m_hasDetails = !d.isEmpty();
    const auto readings
        = [](const char* section, const QList<DeviceDetails::Reading>& list, const char* key) {
              QVariantList out;
              const QString name = QString::fromLatin1(section);
              for (const auto& r : list)
                  out.append(QVariantMap {
                      { QStringLiteral("id"), r.id },
                      { QStringLiteral("title"), labels::detailName(name, r.id) },
                      { QStringLiteral("icon"), labels::detailIcon(name, r.id) },
                      { QString::fromLatin1(key), r.value },
                  });
              return out;
          };
    QVariantList packs;
    for (const DeviceDetails::Pack& p : d.packs) {
        QVariantMap m {
            { QStringLiteral("id"), p.id },
            { QStringLiteral("title"), labels::detailName(QStringLiteral("packs"), p.id) },
        };
        const auto put = [&m](const char* key, const auto& v) {
            if (v)
                m.insert(QString::fromLatin1(key), *v);
        };
        put("charge", p.charge);
        put("health", p.health);
        put("cycles", p.cycles);
        put("temperature", p.temperature);
        put("voltage", p.voltage);
        put("current", p.current);
        put("fullCapacity", p.fullCapacity);
        put("designCapacity", p.designCapacity);
        put("remainingCapacity", p.remainingCapacity);
        put("cellMin", p.cellMin);
        put("cellMax", p.cellMax);
        if (p.voltage && p.current)
            m.insert(QStringLiteral("watts"), *p.voltage * *p.current);
        if (p.voltage && p.remainingCapacity)
            m.insert(QStringLiteral("storedWh"), *p.voltage * *p.remainingCapacity);
        packs << m;
    }
    m_details = {
        { QStringLiteral("inputs"), readings("inputs", d.inputs, "watts") },
        { QStringLiteral("outputs"), readings("outputs", d.outputs, "watts") },
        { QStringLiteral("packs"), packs },
        { QStringLiteral("energy"), readings("energy", d.energy, "wh") },
        { QStringLiteral("temperatures"), readings("temperatures", d.temperatures, "value") },
    };
    if (d.chargeMinutes)
        m_details.insert(QStringLiteral("chargeMinutes"), static_cast<double>(*d.chargeMinutes));
    if (previousGroups != m_groups)
        emit groupsChanged();
    emit changed();
}

QVariantMap DeviceControls::confirmation(const QString& id, double value) const
{
    if (!m_context)
        return {};
    const QList<DeviceControl> list = m_context->monitor()->controls(m_id);
    const auto c = std::ranges::find(list, id, &DeviceControl::id);
    if (c == list.end())
        return {};
    const QString device = deviceName();
    const QString name = text::controlName(id);
    const QString from = c->value ? text::controlValue(*c, *c->value) : tr("not reported");
    const QString to = text::controlValue(*c, value);
    const QString warning = labels::controlWarning(*c, value, device);
    const bool twice = !warning.isEmpty();
    QString confirm = tr("Change");
    if (c->kind == ControlKind::Toggle)
        confirm = value != 0 ? tr("Turn on") : tr("Turn off");
    return {
        //: %1 is a setting ("Charge limit"), %2 the device it belongs to ("Shed station").
        { QStringLiteral("title"), tr("Change %1 on %2?").arg(name, device) },
        { QStringLiteral("message"),
            tr("From %1 to %2. It can take a few seconds for the device to show the change.").arg(from, to) },
        { QStringLiteral("confirmText"), twice ? tr("Continue") : confirm },
        { QStringLiteral("twice"), twice },
        { QStringLiteral("warningTitle"), tr("Really change %1?").arg(name) },
        { QStringLiteral("warningMessage"), warning },
        { QStringLiteral("warningConfirmText"), tr("Yes, change it") },
        { QStringLiteral("icon"), labels::controlIcon(id) },
    };
}

bool DeviceControls::apply(const QString& id, double value)
{
    if (!m_context)
        return false;
    QString error = blockedReason();
    if (error.isEmpty() && m_context->monitor()->applyControl(m_id, id, value, &error)) {
        m_outcomes.remove(id);
        reload();
        return true;
    }
    setOutcome(id, ControlResult::Failed, error);
    reload();
    emit finished(id, false, error);
    return false;
}

}  // namespace nutty
