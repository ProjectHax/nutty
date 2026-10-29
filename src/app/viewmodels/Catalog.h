// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

namespace nutty {

class AppContext;

// The configured devices, channels, rules and groups as plain lists for
// QML, as the singleton `Catalog`, plus the one-click edits on them (enable,
// remove, groups). The editors (DeviceEditor, ChannelEditor, RuleEditor)
// handle the detailed forms.
class Catalog : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // [{ id, name, type, typeName, icon, enabled }]
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
    // [{ id, name, type, typeName, icon, enabled, eventCount, description }]
    Q_PROPERTY(QVariantList channels READ channels NOTIFY changed)
    // [{ id, name, enabled, scope, target, targetName, condition, threshold,
    //    conditionText, action, actionName, countdownSec, description, available, caveat }]
    Q_PROPERTY(QVariantList rules READ rules NOTIFY changed)
    // [{ id, name, members: [ids], memberNames }]
    Q_PROPERTY(QVariantList groups READ groups NOTIFY changed)
    Q_PROPERTY(QVariantList sourceTypes READ sourceTypes CONSTANT)
    Q_PROPERTY(QVariantList channelTypes READ channelTypes CONSTANT)
    Q_PROPERTY(QVariantList eventTypes READ eventTypes CONSTANT)
    Q_PROPERTY(QVariantList conditions READ conditions CONSTANT)
    Q_PROPERTY(QVariantList actionTypes READ actionTypes CONSTANT)
    Q_PROPERTY(QVariantList scopes READ scopes CONSTANT)
    Q_PROPERTY(QVariantList deviceKinds READ deviceKinds CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit Catalog(AppContext* context = nullptr, QObject* parent = nullptr);
    static Catalog* create(QQmlEngine*, QJSEngine*);

    QVariantList devices() const;
    QVariantList channels() const;
    QVariantList rules() const;
    QVariantList groups() const;
    QVariantList sourceTypes() const;
    QVariantList channelTypes() const;
    QVariantList eventTypes() const;
    QVariantList conditions() const;
    QVariantList actionTypes() const;
    QVariantList scopes() const;
    QVariantList deviceKinds() const;
    QString lastError() const;

    Q_INVOKABLE QString deviceName(const QString& id) const;
    Q_INVOKABLE bool setDeviceEnabled(const QString& id, bool enabled);
    Q_INVOKABLE bool removeDevice(const QString& id);
    Q_INVOKABLE bool moveDevice(const QString& id, int delta);
    Q_INVOKABLE bool setChannelEnabled(const QString& id, bool enabled);
    Q_INVOKABLE bool removeChannel(const QString& id);
    Q_INVOKABLE bool setRuleEnabled(const QString& id, bool enabled);
    Q_INVOKABLE bool removeRule(const QString& id);
    // Creates the group when id is empty; returns its id (empty on failure).
    Q_INVOKABLE QString saveGroup(const QString& id, const QString& name, const QStringList& members);
    Q_INVOKABLE bool removeGroup(const QString& id);

signals:
    void changed();
    void lastErrorChanged();

private:
    QPointer<AppContext> m_context;
};

}  // namespace nutty
