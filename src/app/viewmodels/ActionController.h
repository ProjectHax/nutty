// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

namespace nutty {

class AppContext;

// The pending power action and the controls around it, as the singleton
// `Actions`: the countdown, cancel, snooze, dry run and rule simulation.
class ActionController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Actions)
    QML_SINGLETON
    Q_PROPERTY(bool pending READ pending NOTIFY pendingChanged)
    // shutdown, hibernate, sleep, command, notify
    Q_PROPERTY(QString action READ action NOTIFY pendingChanged)
    Q_PROPERTY(QString actionName READ actionName NOTIFY pendingChanged)
    // "Shutting down in 42 s"
    Q_PROPERTY(QString title READ title NOTIFY remainingChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY pendingChanged)
    Q_PROPERTY(QString targetName READ targetName NOTIFY pendingChanged)
    Q_PROPERTY(QString ruleName READ ruleName NOTIFY pendingChanged)
    Q_PROPERTY(QString reason READ reason NOTIFY pendingChanged)
    Q_PROPERTY(int remaining READ remaining NOTIFY remainingChanged)
    Q_PROPERTY(int total READ total NOTIFY pendingChanged)
    Q_PROPERTY(bool cancellable READ cancellable NOTIFY pendingChanged)
    Q_PROPERTY(bool fsd READ fsd NOTIFY pendingChanged)
    // The pending action is a simulation (dry run or "Simulate").
    Q_PROPERTY(bool simulation READ simulation NOTIFY pendingChanged)
    Q_PROPERTY(bool dryRun READ dryRun WRITE setDryRun NOTIFY dryRunChanged)
    Q_PROPERTY(bool dryRunForced READ dryRunForced CONSTANT)
    Q_PROPERTY(bool snoozed READ snoozed NOTIFY snoozeChanged)
    Q_PROPERTY(QDateTime snoozedUntil READ snoozedUntil NOTIFY snoozeChanged)
    // macOS only: shutting down needs the user's Automation consent for System Events.
    Q_PROPERTY(bool shutdownNeedsPermission READ shutdownNeedsPermission CONSTANT)

public:
    explicit ActionController(AppContext* context = nullptr, QObject* parent = nullptr);
    static ActionController* create(QQmlEngine*, QJSEngine*);

    bool pending() const;
    QString action() const;
    QString actionName() const;
    QString title() const;
    QString deviceName() const;
    QString targetName() const;
    QString ruleName() const;
    QString reason() const;
    int remaining() const;
    int total() const;
    bool cancellable() const;
    bool fsd() const;
    bool simulation() const;
    bool dryRun() const;
    void setDryRun(bool enabled);
    bool dryRunForced() const;
    bool snoozed() const;
    QDateTime snoozedUntil() const;
    bool shutdownNeedsPermission() const;

    Q_INVOKABLE bool cancel();
    Q_INVOKABLE void snooze(int minutes);
    Q_INVOKABLE void resume();
    Q_INVOKABLE bool simulate(const QString& ruleId);
    // { available, reason } for an action key (shutdown, sleep, ...).
    Q_INVOKABLE QVariantMap capability(const QString& action) const;
    // Asks macOS for the shutdown permission now, while someone is at the
    // machine. Returns an empty string on success, else why it failed.
    Q_INVOKABLE QString requestShutdownPermission();

    // "Shutting down in 42 s", "Simulation: going to sleep in 5 s".
    static QString countdownTitle(const QString& action, int remaining, bool simulation);

signals:
    void pendingChanged();
    void remainingChanged();
    void dryRunChanged();
    void snoozeChanged();

private:
    void onPendingChanged();

    QPointer<AppContext> m_context;
    QTimer m_ticker;
    int m_lastRemaining = -1;
};

}  // namespace nutty
