// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "app/viewmodels/RuleEditor.h"

#include "app/viewmodels/AppContext.h"
#include "app/viewmodels/Labels.h"
#include "core/engine/EventText.h"
#include "core/engine/Monitor.h"
#include "platform/power/PowerActions.h"

#include <algorithm>

namespace nutty {

RuleEditor::RuleEditor(QObject* parent)
    : RuleEditor(AppContext::instance(), parent)
{
}

RuleEditor::RuleEditor(AppContext* context, QObject* parent)
    : QObject(parent)
    , m_context(context)
{
    if (m_context)
        connect(m_context, &AppContext::configChanged, this, &RuleEditor::edited);  // target names
    reset();
}

QString RuleEditor::ruleId() const
{
    return idString(m_rule.id);
}

void RuleEditor::setName(const QString& name)
{
    if (name == m_rule.name)
        return;
    m_rule.name = name;
    markEdited();
}

void RuleEditor::setEnabled(bool enabled)
{
    if (enabled == m_rule.enabled)
        return;
    m_rule.enabled = enabled;
    markEdited();
}

QString RuleEditor::scope() const
{
    return enumKey(m_rule.scope);
}

void RuleEditor::setScope(const QString& scope)
{
    const RuleScope s = enumFromKey(scope, m_rule.scope);
    if (s == m_rule.scope)
        return;
    const bool wasDevice = m_rule.scope == RuleScope::Device;
    m_rule.scope = s;
    if (wasDevice != (s == RuleScope::Device)) {
        const QVariantList list = targets();
        m_rule.target
            = list.isEmpty() ? QUuid() : parseId(list.first().toMap().value(QStringLiteral("id")).toString());
    }
    markEdited();
}

QString RuleEditor::target() const
{
    return idString(m_rule.target);
}

void RuleEditor::setTarget(const QString& id)
{
    const QUuid t = parseId(id);
    if (t == m_rule.target)
        return;
    m_rule.target = t;
    markEdited();
}

QVariantList RuleEditor::targets() const
{
    QVariantList list;
    if (!m_context)
        return list;
    const AppConfig& config = m_context->config();
    if (m_rule.scope == RuleScope::Device) {
        for (const DeviceConfig& d : config.devices)
            list.append(QVariantMap {
                { QStringLiteral("id"), idString(d.id) }, { QStringLiteral("name"), text::deviceName(d) } });
    } else {
        for (const DeviceGroup& g : config.groups)
            list.append(
                QVariantMap { { QStringLiteral("id"), idString(g.id) }, { QStringLiteral("name"), g.name } });
    }
    return list;
}

QString RuleEditor::condition() const
{
    return enumKey(m_rule.condition);
}

void RuleEditor::setCondition(const QString& condition)
{
    const ConditionType c = enumFromKey(condition, m_rule.condition);
    if (c == m_rule.condition)
        return;
    m_rule.condition = c;
    // A sensible threshold for the new unit.
    switch (c) {
    case ConditionType::ChargeBelow:
        m_rule.threshold = 20;
        break;
    case ConditionType::RuntimeBelow:
        m_rule.threshold = 300;
        break;
    case ConditionType::OnBatteryFor:
        m_rule.threshold = 600;
        break;
    default:
        m_rule.threshold = 0;
        break;
    }
    markEdited();
}

void RuleEditor::setThreshold(double value)
{
    value = std::max(0.0, value);
    if (m_rule.condition == ConditionType::ChargeBelow)
        value = std::min(100.0, value);
    if (qFuzzyCompare(value + 1, m_rule.threshold + 1))
        return;
    m_rule.threshold = value;
    markEdited();
}

QString RuleEditor::unit() const
{
    return labels::conditionUnit(m_rule.condition);
}

QString RuleEditor::action() const
{
    return enumKey(m_rule.action);
}

void RuleEditor::setAction(const QString& action)
{
    const ActionType a = enumFromKey(action, m_rule.action);
    if (a == m_rule.action)
        return;
    m_rule.action = a;
    markEdited();
}

void RuleEditor::setCountdownSec(int sec)
{
    sec = std::clamp(sec, 0, 3600);
    if (sec == m_rule.countdownSec)
        return;
    m_rule.countdownSec = sec;
    markEdited();
}

bool RuleEditor::hasCountdown() const
{
    return m_rule.condition != ConditionType::ForcedShutdown && m_rule.action != ActionType::Notify;
}

void RuleEditor::setCommand(const QString& command)
{
    if (command == m_rule.command)
        return;
    m_rule.command = command;
    markEdited();
}

QVariantMap RuleEditor::capability() const
{
    const PowerCapability cap = PowerActions::capability(m_rule.action);
    return { { QStringLiteral("available"), cap.available }, { QStringLiteral("reason"), cap.reason } };
}

QString RuleEditor::targetName() const
{
    if (!m_context)
        return {};
    const AppConfig& config = m_context->config();
    if (m_rule.scope == RuleScope::Device) {
        const DeviceConfig* d = config.device(m_rule.target);
        return d ? text::deviceName(*d) : QString();
    }
    const DeviceGroup* g = config.group(m_rule.target);
    return g ? g->name : QString();
}

QString RuleEditor::suggestedName() const
{
    const QString what = labels::conditionText(m_rule.condition, m_rule.threshold);
    const QString who = targetName();
    return who.isEmpty() ? tr("%1: %2").arg(text::actionName(m_rule.action), what)
                         : tr("%1 on %2: %3").arg(text::actionName(m_rule.action), who, what);
}

QString RuleEditor::summary() const
{
    const QString who = targetName().isEmpty() ? tr("the device") : targetName();
    QString when;
    switch (m_rule.scope) {
    case RuleScope::Device:
        when = tr("when %1 reports: %2")
                   .arg(who, labels::conditionText(m_rule.condition, m_rule.threshold).toLower());
        break;
    case RuleScope::GroupAny:
        when = tr("when any device in %1 reports: %2")
                   .arg(who, labels::conditionText(m_rule.condition, m_rule.threshold).toLower());
        break;
    case RuleScope::GroupAll:
        when = tr("when every device in %1 reports: %2")
                   .arg(who, labels::conditionText(m_rule.condition, m_rule.threshold).toLower());
        break;
    }
    if (m_rule.action == ActionType::Notify)
        return tr("Send a notification %1.").arg(when);
    if (!hasCountdown())
        return tr("%1 right away %2.").arg(text::actionVerb(m_rule.action), when);
    return tr("%1 after a %2 countdown %3.")
        .arg(text::actionVerb(m_rule.action), text::duration(m_rule.countdownSec), when);
}

bool RuleEditor::load(const QString& id)
{
    if (!m_context)
        return false;
    const auto& rules = m_context->config().rules;
    const auto it = std::ranges::find(rules, parseId(id), &Rule::id);
    if (it == rules.end()) {
        reset();
        return false;
    }
    m_rule = *it;
    m_isNew = false;
    m_dirty = false;
    setError({});
    emit loaded();
    emit edited();
    return true;
}

void RuleEditor::reset(const QString& target)
{
    m_rule = Rule {};
    m_rule.id = QUuid::createUuid();
    if (m_context) {
        const AppConfig& config = m_context->config();
        const QUuid t = parseId(target);
        if (config.group(t)) {
            m_rule.scope = RuleScope::GroupAny;
            m_rule.target = t;
        } else if (config.device(t)) {
            m_rule.target = t;
        } else if (!config.devices.isEmpty()) {
            m_rule.target = config.devices.first().id;
        }
    }
    m_rule.name.clear();
    m_isNew = true;
    m_dirty = false;
    setError({});
    emit loaded();
    emit edited();
}

Rule RuleEditor::current() const
{
    Rule r = m_rule;
    r.name = r.name.trimmed();
    if (r.name.isEmpty())
        r.name = suggestedName();
    r.command = r.command.trimmed();
    return r;
}

bool RuleEditor::save()
{
    if (!m_context)
        return false;
    const Rule rule = current();
    if (rule.target.isNull() || targetName().isEmpty()) {
        setError(rule.scope == RuleScope::Device ? tr("Choose a device.") : tr("Choose a group."));
        return false;
    }
    if (rule.action == ActionType::Command && rule.command.isEmpty()) {
        setError(tr("Enter the command to run."));
        return false;
    }
    AppConfig config = m_context->config();
    const auto it = std::ranges::find(config.rules, rule.id, &Rule::id);
    if (it == config.rules.end())
        config.rules.append(rule);
    else
        *it = rule;
    const bool ok = m_context->updateConfig(config);
    setError(ok ? QString() : m_context->lastError());
    m_rule = rule;
    m_isNew = false;
    m_dirty = false;
    emit loaded();
    emit edited();
    emit saved(idString(rule.id));
    return ok;
}

bool RuleEditor::remove()
{
    if (!m_context || m_isNew)
        return false;
    AppConfig config = m_context->config();
    const QUuid id = m_rule.id;
    config.rules.removeIf([&](const Rule& r) { return r.id == id; });
    const bool ok = m_context->updateConfig(config);
    emit removed(idString(id));
    return ok;
}

bool RuleEditor::simulate()
{
    if (!m_context)
        return false;
    if (m_dirty || m_isNew) {
        if (!save())
            return false;
    }
    return m_context->monitor()->simulateRule(m_rule.id);
}

void RuleEditor::markEdited()
{
    m_dirty = true;
    emit edited();
}

void RuleEditor::setError(const QString& error)
{
    if (error == m_error)
        return;
    m_error = error;
    emit errorChanged();
}

}  // namespace nutty
