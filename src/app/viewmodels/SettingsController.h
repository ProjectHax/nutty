// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Config.h"

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

namespace nutty {

class AppContext;

// The General and Actions settings, as the singleton `Preferences`. Every
// setter changes a working copy and saves it right away (save() writes the
// ConfigStore and applies the change live); the application reacts to the
// config change (theme, tray icon, autostart, language).
class SettingsController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Preferences)
    QML_SINGLETON
    // System, Light, Dark
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY changed)
    // System, On, Off
    Q_PROPERTY(QString motion READ motion WRITE setMotion NOTIFY changed)
    Q_PROPERTY(bool translucentWindow READ translucentWindow WRITE setTranslucentWindow NOTIFY changed)
    Q_PROPERTY(bool useSystemTitleBar READ useSystemTitleBar WRITE setUseSystemTitleBar NOTIFY changed)
    Q_PROPERTY(bool autostart READ autostart WRITE setAutostart NOTIFY changed)
    Q_PROPERTY(bool keepWindowLoaded READ keepWindowLoaded WRITE setKeepWindowLoaded NOTIFY changed)
    // Acorn, Battery
    Q_PROPERTY(QString trayIcon READ trayIcon WRITE setTrayIcon NOTIFY changed)
    Q_PROPERTY(bool monochromeTrayIcon READ monochromeTrayIcon WRITE setMonochromeTrayIcon NOTIFY changed)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY changed)
    Q_PROPERTY(int sampleRetentionDays READ sampleRetentionDays WRITE setSampleRetentionDays NOTIFY changed)
    Q_PROPERTY(int eventRetentionDays READ eventRetentionDays WRITE setEventRetentionDays NOTIFY changed)

    Q_PROPERTY(int safetyMarginSec READ safetyMarginSec WRITE setSafetyMarginSec NOTIFY changed)
    Q_PROPERTY(int stableOnlineSec READ stableOnlineSec WRITE setStableOnlineSec NOTIFY changed)
    Q_PROPERTY(int deadTimeSec READ deadTimeSec WRITE setDeadTimeSec NOTIFY changed)
    Q_PROPERTY(int wakeGraceSec READ wakeGraceSec WRITE setWakeGraceSec NOTIFY changed)
    Q_PROPERTY(int fsdDelaySec READ fsdDelaySec WRITE setFsdDelaySec NOTIFY changed)

    // [{ code, name }]; code "" follows the system.
    Q_PROPERTY(QVariantList languages READ languages CONSTANT)
    Q_PROPERTY(bool secureStorage READ secureStorage CONSTANT)
    Q_PROPERTY(QString secretBackend READ secretBackend CONSTANT)
    // Shown when credentials can't go to a keychain.
    Q_PROPERTY(QString secretWarning READ secretWarning CONSTANT)
    Q_PROPERTY(bool autostartSupported READ autostartSupported CONSTANT)
    // The Linux-only options (custom title bar).
    Q_PROPERTY(bool titleBarOption READ titleBarOption CONSTANT)
    Q_PROPERTY(bool translucencyOption READ translucencyOption CONSTANT)
    Q_PROPERTY(QString configPath READ configPath CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit SettingsController(AppContext* context = nullptr, QObject* parent = nullptr);
    static SettingsController* create(QQmlEngine*, QJSEngine*);

    QString theme() const;
    void setTheme(const QString& theme);
    QString motion() const;
    void setMotion(const QString& motion);
    bool translucentWindow() const { return m_general.translucentWindow; }
    void setTranslucentWindow(bool on);
    bool useSystemTitleBar() const { return m_general.useSystemTitleBar; }
    void setUseSystemTitleBar(bool on);
    bool autostart() const { return m_general.autostart; }
    void setAutostart(bool on);
    bool keepWindowLoaded() const { return m_general.keepWindowLoaded; }
    void setKeepWindowLoaded(bool on);
    QString trayIcon() const;
    void setTrayIcon(const QString& style);
    bool monochromeTrayIcon() const { return m_general.monochromeTrayIcon; }
    void setMonochromeTrayIcon(bool on);
    QString language() const { return m_general.language; }
    void setLanguage(const QString& language);
    int sampleRetentionDays() const { return m_general.sampleRetentionDays; }
    void setSampleRetentionDays(int days);
    int eventRetentionDays() const { return m_general.eventRetentionDays; }
    void setEventRetentionDays(int days);

    int safetyMarginSec() const { return m_actions.safetyMarginSec; }
    void setSafetyMarginSec(int s);
    int stableOnlineSec() const { return m_actions.stableOnlineSec; }
    void setStableOnlineSec(int s);
    int deadTimeSec() const { return m_actions.deadTimeSec; }
    void setDeadTimeSec(int s);
    int wakeGraceSec() const { return m_actions.wakeGraceSec; }
    void setWakeGraceSec(int s);
    int fsdDelaySec() const { return m_actions.fsdDelaySec; }
    void setFsdDelaySec(int s);

    QVariantList languages() const;
    bool secureStorage() const;
    QString secretBackend() const;
    QString secretWarning() const;
    bool autostartSupported() const;
    bool titleBarOption() const;
    bool translucencyOption() const;
    QString configPath() const;
    QString lastError() const;

    // Writes the working copy to the ConfigStore and applies it live.
    Q_INVOKABLE bool save();
    // Drops unsaved changes and reloads from the running config.
    Q_INVOKABLE void revert();

    // The languages there are translations for, by scanning :/i18n.
    static QStringList availableLanguageCodes();

signals:
    void changed();
    void lastErrorChanged();

private:
    template <typename T, typename V> void assign(T& field, const V& value);

    QPointer<AppContext> m_context;
    GeneralSettings m_general;
    ActionSettings m_actions;
    bool m_saving = false;
};

}  // namespace nutty
