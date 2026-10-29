// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Config.h"
#include "core/model/Source.h"

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace nutty {

class AppContext;

// The add/edit device form. load() an existing device or reset() to a new
// one of a type, edit the properties and values (the per-type settings and
// secrets, described by `fields`), then save().
//
// discover() asks the endpoint what it offers (UPS names, serials, OS power
// sources) and testConnection() tries the settings with a temporary Source.
//
// The account details (the fields marked `account`: server, login, keys) are
// usually the same for several devices. A new device starts with those of the
// last device of its type, and saving a change to them can carry it over to
// the other devices that shared them.
class DeviceEditor : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString deviceId READ deviceId NOTIFY loaded)
    Q_PROPERTY(bool isNew READ isNew NOTIFY loaded)
    Q_PROPERTY(QString type READ type WRITE setType NOTIFY typeChanged)
    Q_PROPERTY(QString typeName READ typeName NOTIFY typeChanged)
    Q_PROPERTY(QVariantList fields READ fields NOTIFY typeChanged)
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY edited)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY edited)
    Q_PROPERTY(int pollIntervalSec READ pollIntervalSec WRITE setPollIntervalSec NOTIFY edited)
    Q_PROPERTY(int defaultPollIntervalSec READ defaultPollIntervalSec NOTIFY typeChanged)
    Q_PROPERTY(QString kind READ kind WRITE setKind NOTIFY edited)
    Q_PROPERTY(QString customImage READ customImage WRITE setCustomImage NOTIFY edited)
    Q_PROPERTY(QUrl imageSource READ imageSource NOTIFY edited)
    Q_PROPERTY(int notifyChargeBelow READ notifyChargeBelow WRITE setNotifyChargeBelow NOTIFY edited)
    Q_PROPERTY(
        int notifyRuntimeBelowSec READ notifyRuntimeBelowSec WRITE setNotifyRuntimeBelowSec NOTIFY edited)
    Q_PROPERTY(bool commLostCritical READ commLostCritical WRITE setCommLostCritical NOTIFY edited)
    // Channel ids this device notifies; empty = every channel.
    Q_PROPERTY(QStringList channels READ channels WRITE setChannels NOTIFY edited)
    Q_PROPERTY(bool dirty READ dirty NOTIFY edited)

    Q_PROPERTY(bool discoverySupported READ discoverySupported NOTIFY typeChanged)
    Q_PROPERTY(bool discovering READ discovering NOTIFY discoveryChanged)
    // [{ id, name, description, online, added }]; `added` when it is already
    // set up as another device
    Q_PROPERTY(QVariantList discovered READ discovered NOTIFY discoveryChanged)
    Q_PROPERTY(QString discoveryError READ discoveryError NOTIFY discoveryChanged)

    // The device a new one took its account details from.
    Q_PROPERTY(QString reusedFrom READ reusedFrom NOTIFY sharingChanged)
    // The other devices with the same account details as this one (as saved).
    Q_PROPERTY(QStringList sharedWith READ sharedWith NOTIFY sharingChanged)
    // A sentence about either of the two, or empty.
    Q_PROPERTY(QString accountNote READ accountNote NOTIFY sharingChanged)
    // Whether the account details differ from the saved ones.
    Q_PROPERTY(bool accountEdited READ accountEdited NOTIFY valuesChanged)
    // Whether save() changes them for `sharedWith` too.
    Q_PROPERTY(bool applyToShared READ applyToShared WRITE setApplyToShared NOTIFY sharingChanged)

    Q_PROPERTY(bool testing READ testing NOTIFY testChanged)
    Q_PROPERTY(bool testDone READ testDone NOTIFY testChanged)
    Q_PROPERTY(bool testOk READ testOk NOTIFY testChanged)
    Q_PROPERTY(QString testResult READ testResult NOTIFY testChanged)
    Q_PROPERTY(int testTimeoutMs READ testTimeoutMs WRITE setTestTimeoutMs NOTIFY testChanged)

    // Why save() failed (validation or storage).
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit DeviceEditor(QObject* parent = nullptr);
    DeviceEditor(AppContext* context, QObject* parent);
    ~DeviceEditor() override;

    QString deviceId() const;
    bool isNew() const { return m_isNew; }
    QString type() const;
    void setType(const QString& type);
    QString typeName() const;
    QVariantList fields() const;
    QVariantMap values() const;
    QString name() const { return m_device.name; }
    void setName(const QString& name);
    bool enabled() const { return m_device.enabled; }
    void setEnabled(bool enabled);
    int pollIntervalSec() const { return m_device.pollIntervalSec; }
    void setPollIntervalSec(int sec);
    int defaultPollIntervalSec() const;
    QString kind() const;
    void setKind(const QString& kind);
    QString customImage() const { return m_device.customImage; }
    void setCustomImage(const QString& file);
    QUrl imageSource() const;
    int notifyChargeBelow() const { return m_device.notifyChargeBelow; }
    void setNotifyChargeBelow(int percent);
    int notifyRuntimeBelowSec() const { return m_device.notifyRuntimeBelowSec; }
    void setNotifyRuntimeBelowSec(int sec);
    bool commLostCritical() const { return m_device.commLostCritical; }
    void setCommLostCritical(bool on);
    QStringList channels() const;
    void setChannels(const QStringList& ids);
    bool dirty() const { return m_dirty; }

    bool discoverySupported() const;
    bool discovering() const { return m_discovery != nullptr; }
    QVariantList discovered() const { return m_discovered; }
    QString discoveryError() const { return m_discoveryError; }

    QString reusedFrom() const { return m_reusedFrom; }
    QStringList sharedWith() const { return m_sharedWith; }
    QString accountNote() const;
    bool accountEdited() const;
    bool applyToShared() const { return m_applyToShared; }
    void setApplyToShared(bool on);

    bool testing() const { return m_testSource != nullptr; }
    bool testDone() const { return m_testDone; }
    bool testOk() const { return m_testOk; }
    QString testResult() const { return m_testResult; }
    int testTimeoutMs() const { return m_testTimer.interval(); }
    void setTestTimeoutMs(int ms);

    QString error() const { return m_error; }

    // Loads an existing device; false (and a new device) for an unknown id.
    Q_INVOKABLE bool load(const QString& id);
    // Starts a new device of `type` (a SourceType key), with the account
    // details of the last device of that type.
    Q_INVOKABLE void reset(const QString& type);
    // Clears the account details a new device took from another.
    Q_INVOKABLE void startBlank();
    Q_INVOKABLE QVariant value(const QString& key) const;
    Q_INVOKABLE void setValue(const QString& key, const QVariant& value);
    // Whether a field is shown, given its showIf condition.
    Q_INVOKABLE bool fieldVisible(const QVariantMap& field) const;
    Q_INVOKABLE void discover();
    // Takes discovered[index]: fills the discover field (and the name if empty).
    Q_INVOKABLE void pick(int index);
    Q_INVOKABLE void testConnection();
    Q_INVOKABLE void cancelTest();
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool remove();

    // The config the form describes right now.
    DeviceConfig current() const;

signals:
    void loaded();
    void typeChanged();
    void valuesChanged();
    void edited();
    void discoveryChanged();
    void sharingChanged();
    void testChanged();
    void errorChanged();
    void saved(const QString& id);
    void removed(const QString& id);

private:
    void markEdited();
    void setError(const QString& error);
    QVariantMap fieldSpec(const QString& key) const;
    void finishTest(bool ok, const QString& result);
    QString validate() const;
    void updateSharing();
    void cancelDiscovery();

    QPointer<AppContext> m_context;
    DeviceConfig m_device;
    bool m_isNew = true;
    bool m_dirty = false;
    QString m_error;

    QPointer<Discovery> m_discovery;
    QVariantList m_discovered;
    QString m_discoveryError;

    QString m_reusedFrom;
    QStringList m_sharedWith;
    QVariantMap m_savedAccount;
    bool m_applyToShared = true;

    Source* m_testSource = nullptr;
    QTimer m_testTimer;
    bool m_testDone = false;
    bool m_testOk = false;
    QString m_testResult;
};

}  // namespace nutty
