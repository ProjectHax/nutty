// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Config.h"

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace nutty {

class AppContext;

// The add/edit notification channel form: type-specific fields (as in
// DeviceEditor), the events it receives, and a test send through the
// Dispatcher that works before the channel is saved.
class ChannelEditor : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString channelId READ channelId NOTIFY loaded)
    Q_PROPERTY(bool isNew READ isNew NOTIFY loaded)
    Q_PROPERTY(QString type READ type WRITE setType NOTIFY typeChanged)
    Q_PROPERTY(QString typeName READ typeName NOTIFY typeChanged)
    Q_PROPERTY(QVariantList fields READ fields NOTIFY typeChanged)
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY edited)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY edited)
    // Event type keys this channel receives.
    Q_PROPERTY(QStringList events READ events WRITE setEvents NOTIFY edited)
    Q_PROPERTY(bool dirty READ dirty NOTIFY edited)
    Q_PROPERTY(bool testing READ testing NOTIFY testChanged)
    Q_PROPERTY(bool testDone READ testDone NOTIFY testChanged)
    Q_PROPERTY(bool testOk READ testOk NOTIFY testChanged)
    Q_PROPERTY(QString testResult READ testResult NOTIFY testChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit ChannelEditor(QObject* parent = nullptr);
    ChannelEditor(AppContext* context, QObject* parent);
    ~ChannelEditor() override;

    QString channelId() const;
    bool isNew() const { return m_isNew; }
    QString type() const;
    void setType(const QString& type);
    QString typeName() const;
    QVariantList fields() const;
    QVariantMap values() const;
    QString name() const { return m_channel.name; }
    void setName(const QString& name);
    bool enabled() const { return m_channel.enabled; }
    void setEnabled(bool enabled);
    QStringList events() const;
    void setEvents(const QStringList& keys);
    bool dirty() const { return m_dirty; }
    bool testing() const { return m_testing; }
    bool testDone() const { return m_testDone; }
    bool testOk() const { return m_testOk; }
    QString testResult() const { return m_testResult; }
    QString error() const { return m_error; }

    Q_INVOKABLE bool load(const QString& id);
    Q_INVOKABLE void reset(const QString& type);
    Q_INVOKABLE QVariant value(const QString& key) const;
    Q_INVOKABLE void setValue(const QString& key, const QVariant& value);
    Q_INVOKABLE bool fieldVisible(const QVariantMap& field) const;
    Q_INVOKABLE bool hasEvent(const QString& key) const;
    Q_INVOKABLE void setEvent(const QString& key, bool on);
    Q_INVOKABLE void sendTest();
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool remove();

    ChannelConfig current() const;

signals:
    void loaded();
    void typeChanged();
    void valuesChanged();
    void edited();
    void testChanged();
    void errorChanged();
    void saved(const QString& id);
    void removed(const QString& id);

private:
    void markEdited();
    void setError(const QString& error);
    QString validate() const;
    void emitAll();

    QPointer<AppContext> m_context;
    ChannelConfig m_channel;
    bool m_isNew = true;
    bool m_dirty = false;
    QString m_error;
    QUuid m_testId;
    bool m_testing = false;
    bool m_testDone = false;
    bool m_testOk = false;
    QString m_testResult;
};

}  // namespace nutty
