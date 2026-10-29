// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/engine/ActionArbiter.h"
#include "core/engine/EventDetector.h"
#include "core/engine/RuleEngine.h"
#include "core/model/Config.h"
#include "core/model/Event.h"
#include "core/model/Source.h"

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QTimer>
#include <QUuid>

#include <functional>
#include <map>
#include <memory>
#include <optional>

namespace nutty {

class HistoryStore;

// The heart of nutty: owns one Source per enabled device, turns their data
// into events (EventDetector), evaluates the rules (RuleEngine), arbitrates
// power actions (ActionArbiter), records history, and tells the app when an
// action is due. The UI binds to its accessors and signals.
class Monitor : public QObject
{
    Q_OBJECT

public:
    // How a device (or everything) is doing, from best to worst. The tray
    // shows the worst of all devices.
    enum class Health {
        Ok,  // on line power, nothing wrong
        Unknown,  // disabled, or no data yet
        Warning,  // replace battery, overload, bypass, alarm, off
        Offline,  // contact lost
        OnBattery,
        Low,  // low battery or below the device's notify threshold
        Critical,  // forced shutdown, or a power action pending for it
    };
    Q_ENUM(Health)

    using Clock = std::function<QDateTime()>;

    // history may be null (no history is recorded); it must outlive the
    // Monitor and already be open, so outages left open by a crash get closed.
    explicit Monitor(HistoryStore* history = nullptr, QObject* parent = nullptr);
    ~Monitor() override;

    // Tests drive time themselves: a fake clock, and tick() by hand.
    void setClock(Clock clock);
    void setAutoTick(bool enabled);
    QDateTime now() const;

    // Creates, restarts or removes sources so they match the config. Only
    // devices whose source settings changed are restarted.
    void applyConfig(const AppConfig& config);
    const AppConfig& config() const { return m_config; }

    // --- devices ---
    QList<QUuid> deviceIds() const;  // config order, disabled devices included
    const DeviceConfig* device(const QUuid& id) const;
    Source* source(const QUuid& id) const;  // null when disabled/unsupported
    DeviceStatus status(const QUuid& id) const;
    const DeviceState* deviceState(const QUuid& id) const;
    ConnectionState connectionState(const QUuid& id) const;
    SourceError lastErrorKind(const QUuid& id) const;
    QString lastError(const QUuid& id) const;
    QDateTime lastUpdate(const QUuid& id) const;
    // Connected, but the data is older than ~2.5 poll intervals.
    bool isStale(const QUuid& id) const;
    bool isCommLost(const QUuid& id) const;
    // When the current outage started, if the device is in one.
    std::optional<QDateTime> episodeStart(const QUuid& id) const;
    // Row id of the current outage in the history DB (0 if none).
    qint64 episodeHistoryId(const QUuid& id) const;
    Health health(const QUuid& id) const;
    Health overallHealth() const;
    // Re-poll every source now.
    void pollAll();

    // --- settings on the devices themselves ---
    // Empty when the device has no source or nothing nutty can change.
    QList<DeviceControl> controls(const QUuid& id) const;
    DeviceDetails details(const QUuid& id) const;
    // Sends a change the user has confirmed. False, with the reason in
    // `error`, when it can't be sent; otherwise controlFinished() follows and
    // the outcome goes into the history.
    bool applyControl(const QUuid& id, const QString& control, double value, QString* error = nullptr);

    // --- actions ---
    std::optional<PendingAction> pendingAction() const;
    // User cancel. False when nothing cancellable is pending.
    bool cancelPendingAction();
    // Stops scheduling power actions for a while (FSD still acts) and cancels
    // a pending cancellable one. seconds <= 0 ends the snooze.
    void snoozeActions(int seconds);
    void clearSnooze();
    QDateTime snoozedUntil() const;
    // Dry run: due actions are logged as simulated instead of being handed to
    // the app.
    void setDryRun(bool enabled);
    bool dryRun() const { return m_dryRun; }
    // Runs a rule's action through the full countdown flow as a simulation.
    bool simulateRule(const QUuid& ruleId);
    // The app reports how executing an actionDue() went.
    void reportActionResult(const PendingAction& action, bool ok, const QString& error = {});
    // Right after the OS accepted a shutdown: lets NUT secondaries log out.
    void prepareSourcesForShutdown();

    // --- sleep/wake ---
    void notifySleeping();
    void notifyResumed();
    QDateTime wakeGraceUntil() const { return m_wakeGraceUntil; }

    // Advances timers (deadtime, stable line power, countdowns). Runs every
    // second on its own unless auto-tick is off.
    void tick();

signals:
    void devicesChanged();
    void deviceStatusChanged(const QUuid& id);
    void deviceConnectionChanged(const QUuid& id);
    void eventRaised(const nutty::Event& event);
    void pendingActionChanged();
    void snoozeChanged();
    // A power action's countdown ran out: execute it, then reportActionResult().
    void actionDue(const nutty::PendingAction& action);
    void deviceControlsChanged(const QUuid& id);
    void controlFinished(const QUuid& id, const QString& control, double value, nutty::ControlResult result,
        const QString& error);

private:
    struct Entry {
        DeviceConfig config;
        std::unique_ptr<Source> source;
        QDateTime lastSampleAt;
        qint64 episodeId = 0;  // history row of the current outage
    };

    Entry* entry(const QUuid& id);
    const Entry* entry(const QUuid& id) const;
    void createSource(Entry& e);
    void destroySource(Entry& e, bool immediately = false);

    void onStatus(const QUuid& id, const DeviceStatus& status);
    void onError(const QUuid& id, SourceError kind, const QString& message);
    void onControlFinished(
        const QUuid& id, const QString& control, double value, ControlResult result, const QString& error);
    void handleEvents(Entry* e, const QList<Event>& events);
    void recordEvent(Event event);
    void evaluateRules();
    void checkPending();
    void raiseActionEvent(EventType type, const PendingAction& action, const QString& detail = {});
    void prune();

    HistoryStore* m_history = nullptr;
    Clock m_clock;
    AppConfig m_config;
    QList<QUuid> m_order;
    std::map<QUuid, Entry> m_entries;
    EventDetector m_detector;
    RuleEngine m_rules;
    ActionArbiter m_arbiter;
    QTimer m_ticker;
    QDateTime m_lastPrune;
    QDateTime m_wakeGraceUntil;
    bool m_dryRun = false;
    bool m_sleeping = false;
};

}  // namespace nutty
