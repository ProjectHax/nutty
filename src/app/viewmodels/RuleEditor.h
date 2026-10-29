// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Config.h"

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace nutty {

class AppContext;

// The add/edit power rule form: what to watch (a device, or any/every device
// of a group), the condition, the action and its countdown. `capability`
// says whether the action can run on this system.
class RuleEditor : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString ruleId READ ruleId NOTIFY loaded)
    Q_PROPERTY(bool isNew READ isNew NOTIFY loaded)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY edited)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY edited)
    // Device, GroupAny, GroupAll
    Q_PROPERTY(QString scope READ scope WRITE setScope NOTIFY edited)
    Q_PROPERTY(QString target READ target WRITE setTarget NOTIFY edited)
    // [{ id, name }]: devices or groups, depending on scope
    Q_PROPERTY(QVariantList targets READ targets NOTIFY edited)
    Q_PROPERTY(QString condition READ condition WRITE setCondition NOTIFY edited)
    Q_PROPERTY(double threshold READ threshold WRITE setThreshold NOTIFY edited)
    // %, s, or empty when the condition has no threshold
    Q_PROPERTY(QString unit READ unit NOTIFY edited)
    Q_PROPERTY(QString action READ action WRITE setAction NOTIFY edited)
    Q_PROPERTY(int countdownSec READ countdownSec WRITE setCountdownSec NOTIFY edited)
    // Forced shutdown acts right away, without a countdown.
    Q_PROPERTY(bool hasCountdown READ hasCountdown NOTIFY edited)
    Q_PROPERTY(QString command READ command WRITE setCommand NOTIFY edited)
    // { available, reason }
    Q_PROPERTY(QVariantMap capability READ capability NOTIFY edited)
    // "Shut down after a 60 s countdown when Rack UPS is below 20 %"
    Q_PROPERTY(QString summary READ summary NOTIFY edited)
    Q_PROPERTY(QString suggestedName READ suggestedName NOTIFY edited)
    Q_PROPERTY(bool dirty READ dirty NOTIFY edited)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit RuleEditor(QObject* parent = nullptr);
    RuleEditor(AppContext* context, QObject* parent);

    QString ruleId() const;
    bool isNew() const { return m_isNew; }
    QString name() const { return m_rule.name; }
    void setName(const QString& name);
    bool enabled() const { return m_rule.enabled; }
    void setEnabled(bool enabled);
    QString scope() const;
    void setScope(const QString& scope);
    QString target() const;
    void setTarget(const QString& id);
    QVariantList targets() const;
    QString condition() const;
    void setCondition(const QString& condition);
    double threshold() const { return m_rule.threshold; }
    void setThreshold(double value);
    QString unit() const;
    QString action() const;
    void setAction(const QString& action);
    int countdownSec() const { return m_rule.countdownSec; }
    void setCountdownSec(int sec);
    bool hasCountdown() const;
    QString command() const { return m_rule.command; }
    void setCommand(const QString& command);
    QVariantMap capability() const;
    QString summary() const;
    QString suggestedName() const;
    bool dirty() const { return m_dirty; }
    QString error() const { return m_error; }

    Q_INVOKABLE bool load(const QString& id);
    // A new rule, optionally about a device (or group) right away.
    Q_INVOKABLE void reset(const QString& target = {});
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool remove();
    // Saves, then runs the rule's action through the countdown as a simulation.
    Q_INVOKABLE bool simulate();

    Rule current() const;

signals:
    void loaded();
    void edited();
    void errorChanged();
    void saved(const QString& id);
    void removed(const QString& id);

private:
    void markEdited();
    void setError(const QString& error);
    QString targetName() const;

    QPointer<AppContext> m_context;
    Rule m_rule;
    bool m_isNew = true;
    bool m_dirty = false;
    QString m_error;
};

}  // namespace nutty
