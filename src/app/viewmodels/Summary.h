// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

namespace nutty {

class AppContext;

// How everything is doing, in one line, as the singleton `Summary`: the
// home page banner (level/title/subtitle/action), the overall health and
// device counts.
class Summary : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // ok, info, warning, critical (SummaryBanner.level)
    Q_PROPERTY(QString level READ level NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString subtitle READ subtitle NOTIFY changed)
    Q_PROPERTY(QString iconName READ iconName NOTIFY changed)
    // Button text, and what it does: cancel, addDevice, device:<id>, or empty.
    Q_PROPERTY(QString actionText READ actionText NOTIFY changed)
    Q_PROPERTY(QString actionKey READ actionKey NOTIFY changed)
    // ok, unknown, warning, offline, onBattery, low, critical
    Q_PROPERTY(QString health READ health NOTIFY changed)
    Q_PROPERTY(int deviceCount READ deviceCount NOTIFY changed)
    Q_PROPERTY(int onlineCount READ onlineCount NOTIFY changed)
    Q_PROPERTY(int onBatteryCount READ onBatteryCount NOTIFY changed)
    Q_PROPERTY(int offlineCount READ offlineCount NOTIFY changed)
    Q_PROPERTY(int attentionCount READ attentionCount NOTIFY changed)

public:
    explicit Summary(AppContext* context = nullptr, QObject* parent = nullptr);
    static Summary* create(QQmlEngine*, QJSEngine*);

    QString level() const { return m_level; }
    QString title() const { return m_title; }
    QString subtitle() const { return m_subtitle; }
    QString iconName() const { return m_icon; }
    QString actionText() const { return m_actionText; }
    QString actionKey() const { return m_actionKey; }
    QString health() const { return m_health; }
    int deviceCount() const { return m_deviceCount; }
    int onlineCount() const { return m_online; }
    int onBatteryCount() const { return m_onBattery; }
    int offlineCount() const { return m_offline; }
    int attentionCount() const { return m_attention; }

    // Recomputes everything (normally driven by the monitor's signals).
    Q_INVOKABLE void update();

signals:
    void changed();

private:
    void scheduleUpdate();

    QPointer<AppContext> m_context;
    QTimer m_debounce;
    QTimer m_ticker;  // counts a pending action down
    QString m_level;
    QString m_title;
    QString m_subtitle;
    QString m_icon;
    QString m_actionText;
    QString m_actionKey;
    QString m_health;
    int m_deviceCount = 0;
    int m_online = 0;
    int m_onBattery = 0;
    int m_offline = 0;
    int m_attention = 0;
};

}  // namespace nutty
