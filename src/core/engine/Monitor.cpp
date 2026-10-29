// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/engine/Monitor.h"

#include "core/engine/EventText.h"
#include "core/storage/HistoryStore.h"

#include <QCoreApplication>
#include <QLoggingCategory>

#include <algorithm>

Q_LOGGING_CATEGORY(lcMonitor, "nutty.monitor")

namespace nutty {

namespace {

constexpr int kSampleIntervalSec = 60;  // on line power; every poll while on battery
constexpr int kPruneIntervalSec = 3600;
constexpr StatusFlags kWarningFlags = StatusFlag::ReplaceBattery | StatusFlag::Overload | StatusFlag::Bypass
    | StatusFlag::Off | StatusFlag::Alarm;

bool sourceSettingsChanged(const DeviceConfig& a, const DeviceConfig& b)
{
    return a.type != b.type || a.enabled != b.enabled || a.pollIntervalSec != b.pollIntervalSec
        || a.settings != b.settings || a.secrets != b.secrets;
}

// Detector events worth listing in the outage's timeline.
bool isEpisodeMilestone(EventType type)
{
    switch (type) {
    case EventType::LowBattery:
    case EventType::ChargeBelow:
    case EventType::RuntimeBelow:
    case EventType::ForcedShutdown:
    case EventType::CommLost:
    case EventType::CommRestored:
        return true;
    default:
        return false;
    }
}

}  // namespace

Monitor::Monitor(HistoryStore* history, QObject* parent)
    : QObject(parent)
    , m_history(history)
    , m_clock([] { return QDateTime::currentDateTimeUtc(); })
{
    m_ticker.setInterval(1000);
    connect(&m_ticker, &QTimer::timeout, this, &Monitor::tick);
    m_ticker.start();
    // Outages that were still open when nutty last stopped can't be finished
    // any more; end them at their last sample.
    if (m_history && m_history->isOpen())
        m_history->closeOpenEpisodes();
}

Monitor::~Monitor()
{
    for (auto& [id, e] : m_entries)
        destroySource(e, true);
}

void Monitor::setClock(Clock clock)
{
    m_clock = clock ? std::move(clock) : Clock([] { return QDateTime::currentDateTimeUtc(); });
}

void Monitor::setAutoTick(bool enabled)
{
    if (enabled)
        m_ticker.start();
    else
        m_ticker.stop();
}

QDateTime Monitor::now() const
{
    return m_clock();
}

// --- config -----------------------------------------------------------------

void Monitor::applyConfig(const AppConfig& config)
{
    const auto now = this->now();
    m_config = config;
    m_detector.setSettings(config.actions);
    m_arbiter.setSettings(config.actions);

    m_order.clear();
    for (const auto& d : config.devices)
        m_order << d.id;

    // Removed devices.
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (config.device(it->first)) {
            ++it;
            continue;
        }
        destroySource(it->second);
        m_detector.remove(it->first);
        if (m_history && it->second.episodeId)
            m_history->closeEpisode(it->second.episodeId, now);
        it = m_entries.erase(it);
    }

    for (const auto& d : config.devices) {
        auto [it, inserted] = m_entries.try_emplace(d.id);
        Entry& e = it->second;
        const bool restart = inserted || sourceSettingsChanged(e.config, d);
        e.config = d;
        if (!restart)
            continue;
        destroySource(e);
        if (!d.enabled) {
            // A disabled device is forgotten: its outage (if any) ends here.
            m_detector.remove(d.id);
            if (m_history && e.episodeId)
                m_history->closeEpisode(e.episodeId, now);
            e.episodeId = 0;
            e.lastSampleAt = {};
            continue;
        }
        createSource(e);
    }

    // A countdown whose rule or device is gone has nothing left to act on.
    const auto cancelled = m_arbiter.cancelIf([&](const PendingAction& p) {
        const auto rule = std::ranges::find(m_config.rules, p.ruleId, &Rule::id);
        return rule == m_config.rules.end() || !rule->enabled || !m_config.device(p.deviceId);
    });
    if (cancelled) {
        raiseActionEvent(EventType::ActionCancelled, *cancelled,
            QCoreApplication::translate("nutty::Events", "the rule was changed"));
        emit pendingActionChanged();
    }

    emit devicesChanged();
    prune();
}

Monitor::Entry* Monitor::entry(const QUuid& id)
{
    const auto it = m_entries.find(id);
    return it == m_entries.end() ? nullptr : &it->second;
}

const Monitor::Entry* Monitor::entry(const QUuid& id) const
{
    const auto it = m_entries.find(id);
    return it == m_entries.end() ? nullptr : &it->second;
}

void Monitor::createSource(Entry& e)
{
    e.source = SourceRegistry::create(e.config);
    if (!e.source) {
        qCWarning(lcMonitor) << "no source implementation for" << enumKey(e.config.type);
        return;
    }
    const auto id = e.config.id;
    auto* source = e.source.get();
    connect(source, &Source::statusUpdated, this,
        [this, id](const DeviceStatus& status) { onStatus(id, status); });
    connect(source, &Source::errorOccurred, this,
        [this, id](SourceError kind, const QString& message) { onError(id, kind, message); });
    connect(source, &Source::connectionStateChanged, this, [this, id] { emit deviceConnectionChanged(id); });
    connect(source, &Source::controlsChanged, this, [this, id] { emit deviceControlsChanged(id); });
    connect(source, &Source::controlFinished, this,
        [this, id](const QString& control, double value, ControlResult result, const QString& error) {
            onControlFinished(id, control, value, result, error);
        });
    source->start();
}

void Monitor::destroySource(Entry& e, bool immediately)
{
    if (!e.source)
        return;
    e.source->disconnect(this);
    e.source->stop();
    if (immediately)
        e.source.reset();
    else
        e.source.release()->deleteLater();  // may be inside one of its own signals
}

// --- device accessors -------------------------------------------------------

QList<QUuid> Monitor::deviceIds() const
{
    return m_order;
}

const DeviceConfig* Monitor::device(const QUuid& id) const
{
    return m_config.device(id);
}

Source* Monitor::source(const QUuid& id) const
{
    const auto* e = entry(id);
    return e ? e->source.get() : nullptr;
}

QList<DeviceControl> Monitor::controls(const QUuid& id) const
{
    const Source* s = source(id);
    return s ? s->controls() : QList<DeviceControl> {};
}

DeviceDetails Monitor::details(const QUuid& id) const
{
    const Source* s = source(id);
    return s ? s->details() : DeviceDetails {};
}

bool Monitor::applyControl(const QUuid& id, const QString& control, double value, QString* error)
{
    Source* s = source(id);
    if (!s) {
        if (error)
            *error = QCoreApplication::translate("nutty::Events", "The device is paused.");
        return false;
    }
    return s->applyControl(control, value, error);
}

void Monitor::onControlFinished(
    const QUuid& id, const QString& control, double value, ControlResult result, const QString& error)
{
    DeviceControl spec;
    spec.id = control;
    for (const DeviceControl& c : controls(id)) {
        if (c.id == control)
            spec = c;
    }
    const auto* device = m_config.device(id);
    const auto* state = m_detector.state(id);
    const auto name = text::controlName(control);
    const auto to = text::controlValue(spec, value);
    Event ev = text::makeEvent(
        result == ControlResult::Applied ? EventType::SettingChanged : EventType::SettingFailed, device,
        state && state->hasData ? &state->status : nullptr, now());
    switch (result) {
    case ControlResult::Applied:
        ev.title = QCoreApplication::translate("nutty::Events", "%1 set to %2").arg(name, to);
        ev.message = QCoreApplication::translate("nutty::Events", "Changed from nutty.");
        break;
    case ControlResult::Unconfirmed:
        ev.severity = Severity::Warning;
        ev.title = QCoreApplication::translate("nutty::Events", "%1 change not confirmed").arg(name);
        ev.message = QCoreApplication::translate(
            "nutty::Events", "The change to %1 went out, but the device still reports the old value.")
                         .arg(to);
        if (!error.isEmpty())
            ev.message += u' ' + error;
        break;
    case ControlResult::Failed:
        ev.title = QCoreApplication::translate("nutty::Events", "Couldn't set %1 to %2").arg(name, to);
        ev.message = error;
        break;
    }
    ev.extra.insert(QStringLiteral("control"), control);
    ev.extra.insert(QStringLiteral("value"), value);
    ev.extra.insert(QStringLiteral("result"), enumKey(result));
    recordEvent(ev);
    emit controlFinished(id, control, value, result, error);
}

DeviceStatus Monitor::status(const QUuid& id) const
{
    const auto* s = m_detector.state(id);
    return s && s->hasData ? s->status : DeviceStatus {};
}

const DeviceState* Monitor::deviceState(const QUuid& id) const
{
    return m_detector.state(id);
}

ConnectionState Monitor::connectionState(const QUuid& id) const
{
    const auto* src = source(id);
    if (!src)
        return ConnectionState::Disconnected;
    if (src->connectionState() == ConnectionState::Connected && isStale(id))
        return ConnectionState::Stale;
    return src->connectionState();
}

SourceError Monitor::lastErrorKind(const QUuid& id) const
{
    const auto* e = entry(id);
    if (!e || !e->config.enabled)
        return SourceError::None;
    return e->source ? e->source->lastErrorKind() : SourceError::Config;
}

QString Monitor::lastError(const QUuid& id) const
{
    const auto* e = entry(id);
    if (!e || !e->config.enabled)
        return {};
    if (!e->source)
        return tr("This kind of device isn't supported by this build of nutty.");
    return e->source->lastError();
}

QDateTime Monitor::lastUpdate(const QUuid& id) const
{
    const auto* s = m_detector.state(id);
    return s ? s->lastDataAt : QDateTime {};
}

bool Monitor::isStale(const QUuid& id) const
{
    const auto* e = entry(id);
    const auto* s = m_detector.state(id);
    if (!e || !s || !s->hasData || s->commLost)
        return false;
    const qint64 limitMs = e->config.effectivePollIntervalSec() * 2500LL + 5000;
    return s->lastDataAt.msecsTo(now()) > limitMs;
}

bool Monitor::isCommLost(const QUuid& id) const
{
    const auto* s = m_detector.state(id);
    return s && s->commLost;
}

std::optional<QDateTime> Monitor::episodeStart(const QUuid& id) const
{
    const auto* s = m_detector.state(id);
    if (!s || !s->inEpisode)
        return std::nullopt;
    return s->episodeStart;
}

qint64 Monitor::episodeHistoryId(const QUuid& id) const
{
    const auto* e = entry(id);
    return e ? e->episodeId : 0;
}

Monitor::Health Monitor::health(const QUuid& id) const
{
    const auto* e = entry(id);
    if (!e || !e->config.enabled)
        return Health::Unknown;
    const auto& pending = m_arbiter.pending();
    if (pending && !pending->dryRun && pending->deviceId == id)
        return Health::Critical;

    const auto* s = m_detector.state(id);
    if (!s || !s->hasData) {
        if (!e->source || (s && s->commLost) || e->source->connectionState() == ConnectionState::Error)
            return Health::Offline;
        return Health::Unknown;
    }

    const auto& st = s->status;
    Health h = Health::Ok;
    if (s->fsdActive()) {
        h = Health::Critical;
    } else if (st.has(StatusFlag::LowBattery)
        || (s->onBatteryNow()
            && ((e->config.notifyChargeBelow > 0 && st.charge && *st.charge <= e->config.notifyChargeBelow)
                || (e->config.notifyRuntimeBelowSec > 0 && st.runtime
                    && *st.runtime <= e->config.notifyRuntimeBelowSec)))) {
        h = Health::Low;
    } else if (s->onBatteryNow()) {
        h = Health::OnBattery;
    } else if ((st.flags & kWarningFlags).toInt() != 0 || isStale(id)) {
        h = Health::Warning;
    }
    // Lost contact: at least Offline, worse if the last news was worse.
    if (s->commLost)
        h = std::max(h, Health::Offline);
    return h;
}

Monitor::Health Monitor::overallHealth() const
{
    const auto& pending = m_arbiter.pending();
    if (pending && !pending->dryRun)
        return Health::Critical;
    std::optional<Health> worst;
    for (const auto& [id, e] : m_entries) {
        if (!e.config.enabled)
            continue;
        const auto h = health(id);
        worst = worst ? std::max(*worst, h) : h;
    }
    return worst.value_or(Health::Unknown);
}

void Monitor::pollAll()
{
    for (auto& [id, e] : m_entries) {
        if (e.source)
            e.source->pollNow();
    }
}

// --- data flow --------------------------------------------------------------

void Monitor::onStatus(const QUuid& id, const DeviceStatus& status)
{
    auto* e = entry(id);
    if (!e)
        return;
    const auto now = this->now();
    handleEvents(e, m_detector.onStatus(e->config, status, now));

    const auto* s = m_detector.state(id);
    const bool inEpisode = s && s->inEpisode;
    if (m_history) {
        if (e->episodeId && inEpisode)
            m_history->updateEpisode(e->episodeId, status.charge, status.runtime);
        if (inEpisode || !e->lastSampleAt.isValid() || e->lastSampleAt.secsTo(now) >= kSampleIntervalSec) {
            m_history->addSample(id, status);
            e->lastSampleAt = now;
        }
    }

    emit deviceStatusChanged(id);
    if (!m_sleeping) {
        evaluateRules();
        checkPending();
    }
}

void Monitor::onError(const QUuid& id, SourceError kind, const QString& message)
{
    auto* e = entry(id);
    if (!e)
        return;
    qCInfo(lcMonitor).noquote() << text::deviceName(e->config) << enumKey(kind) << message;
    handleEvents(e, m_detector.onError(e->config, kind, message, now()));
    emit deviceStatusChanged(id);
}

void Monitor::handleEvents(Entry* e, const QList<Event>& events)
{
    for (const auto& event : events) {
        if (m_history) {
            if (event.type == EventType::OnBattery) {
                if (e->episodeId)
                    m_history->closeEpisode(e->episodeId, event.timestamp);
                const auto* s = m_detector.state(e->config.id);
                e->episodeId = m_history->openEpisode(e->config.id, text::deviceName(e->config),
                    s ? s->episodeStart : event.timestamp, event.charge, event.runtime);
            } else if (event.type == EventType::PowerRestored && e->episodeId) {
                // The outage ended when line power came back, not when it was
                // confirmed stable.
                const auto* s = m_detector.state(e->config.id);
                const auto onBatterySec = event.extra.value(QStringLiteral("onBatterySec")).toLongLong();
                const auto end = s && s->episodeStart.isValid() ? s->episodeStart.addSecs(onBatterySec)
                                                                : event.timestamp;
                m_history->closeEpisode(e->episodeId, end);
                e->episodeId = 0;
            } else if (e->episodeId && isEpisodeMilestone(event.type)) {
                m_history->appendEpisodeAction(e->episodeId, event.timestamp, event.title);
            }
        }
        recordEvent(event);
    }
}

void Monitor::recordEvent(Event event)
{
    if (m_history && m_history->isOpen())
        event.id = m_history->addEvent(event);
    emit eventRaised(event);
}

void Monitor::tick()
{
    const auto now = this->now();
    if (!m_sleeping) {
        for (auto& [id, e] : m_entries) {
            if (!e.config.enabled || !e.source)
                continue;
            const auto events = m_detector.tick(e.config, now);
            if (events.isEmpty())
                continue;
            handleEvents(&e, events);
            emit deviceStatusChanged(id);
        }
        evaluateRules();
        checkPending();
    }
    if (m_history && (!m_lastPrune.isValid() || m_lastPrune.secsTo(now) >= kPruneIntervalSec))
        prune();
}

void Monitor::prune()
{
    if (!m_history || !m_history->isOpen())
        return;
    const auto now = this->now();
    m_history->prune(m_config.general.sampleRetentionDays, m_config.general.eventRetentionDays, now);
    m_lastPrune = now;
}

// --- rules and actions ------------------------------------------------------

void Monitor::evaluateRules()
{
    if (m_sleeping)
        return;
    const auto now = this->now();
    const auto triggers = m_rules.evaluate(m_config, m_detector.states(), now, m_wakeGraceUntil);
    for (const auto& t : triggers) {
        if (t.rule.action == ActionType::Notify) {
            // Notify rules have nothing to execute: the event is the action.
            m_rules.markFired(t.rule.id, t.episodeKey);
            const auto* device = m_config.device(t.deviceId);
            const auto* s = m_detector.state(t.deviceId);
            auto ev = text::makeEvent(
                EventType::ActionExecuted, device, s && s->hasData ? &s->status : nullptr, now);
            ev.severity = t.fsd ? Severity::Critical : Severity::Warning;
            ev.title = t.rule.name.trimmed().isEmpty()
                ? QCoreApplication::translate("nutty::Events", "Rule triggered")
                : t.rule.name.trimmed();
            ev.message = QCoreApplication::translate("nutty::Events", "%1: %2").arg(t.targetName, t.reason);
            ev.extra.insert(QStringLiteral("action"), enumKey(ActionType::Notify));
            ev.extra.insert(QStringLiteral("ruleId"), t.rule.id.toString(QUuid::WithoutBraces));
            ev.extra.insert(QStringLiteral("ruleName"), t.rule.name);
            ev.extra.insert(QStringLiteral("target"), t.targetName);
            ev.extra.insert(QStringLiteral("reason"), t.reason);
            if (auto* e = entry(t.deviceId); e && e->episodeId && m_history)
                m_history->appendEpisodeAction(e->episodeId, now, ev.title);
            recordEvent(ev);
            continue;
        }

        auto candidate = m_arbiter.makePending(t, now);
        candidate.dryRun = m_dryRun;
        const auto result = m_arbiter.offer(candidate, now);
        if (result == ActionArbiter::Offer::Snoozed)
            continue;  // not marked: it comes back when the snooze ends
        m_rules.markFired(t.rule.id, t.episodeKey);
        if (result == ActionArbiter::Offer::Ignored)
            continue;
        raiseActionEvent(EventType::ActionScheduled, *m_arbiter.pending());
        emit pendingActionChanged();
    }
}

void Monitor::checkPending()
{
    if (m_sleeping)
        return;
    const auto now = this->now();
    bool changed = false;

    // The outage behind a countdown is over (line power held long enough).
    const auto ended = m_arbiter.cancelIf([&](const PendingAction& p) {
        return !p.episodeKey.isEmpty() && !m_rules.isEpisodeActive(p.episodeKey, m_detector.states());
    });
    if (ended) {
        raiseActionEvent(EventType::ActionCancelled, *ended,
            QCoreApplication::translate("nutty::Events", "power was restored"));
        changed = true;
    }

    std::optional<PendingAction> due = m_arbiter.takeDue(now);
    if (due)
        changed = true;
    if (changed)
        emit pendingActionChanged();
    if (!due)
        return;

    if (m_dryRun || due->dryRun) {
        due->dryRun = true;
        raiseActionEvent(EventType::ActionExecuted, *due);
        return;
    }
    qCWarning(lcMonitor).noquote() << "action due:" << enumKey(due->type) << "rule" << due->ruleName;
    emit actionDue(*due);
}

void Monitor::raiseActionEvent(EventType type, const PendingAction& action, const QString& detail)
{
    const auto now = this->now();
    const auto* device = m_config.device(action.deviceId);
    const auto* s = m_detector.state(action.deviceId);
    auto ev = text::makeEvent(type, device, s && s->hasData ? &s->status : nullptr, now);
    if (!device) {
        ev.deviceId = action.deviceId;
        ev.deviceName = action.deviceName;
    }

    const auto name = text::actionName(action.type);
    const auto verb = text::actionVerb(action.type);
    const auto target = action.targetName.isEmpty() ? action.deviceName : action.targetName;
    const auto about = [&](const QString& what) {
        return what.isEmpty() ? target
                              : QCoreApplication::translate("nutty::Events", "%1: %2").arg(target, what);
    };

    switch (type) {
    case EventType::ActionScheduled:
        ev.title = QCoreApplication::translate("nutty::Events", "%1 in %2")
                       .arg(verb, text::duration(action.remainingSec(now)));
        ev.message = about(action.reason);
        break;
    case EventType::ActionCancelled:
        ev.title = QCoreApplication::translate("nutty::Events", "%1 cancelled").arg(name);
        ev.message = about(detail);
        break;
    case EventType::ActionExecuted:
        if (action.dryRun) {
            ev.severity = Severity::Info;
            ev.title = QCoreApplication::translate("nutty::Events", "%1 simulated").arg(name);
            ev.message = QCoreApplication::translate("nutty::Events", "Dry run: %1 would have run now (%2)")
                             .arg(name, about(action.reason));
        } else {
            ev.title = QCoreApplication::translate("nutty::Events", "%1 started").arg(name);
            ev.message = about(action.reason);
        }
        break;
    case EventType::ActionFailed:
        ev.title = QCoreApplication::translate("nutty::Events", "%1 failed").arg(name);
        ev.message = about(detail);
        break;
    default:
        break;
    }
    if (action.dryRun && type != EventType::ActionExecuted)
        ev.title = QCoreApplication::translate("nutty::Events", "%1 (dry run)").arg(ev.title);
    if (action.dryRun && type == EventType::ActionScheduled)
        ev.severity = Severity::Info;

    ev.extra.insert(QStringLiteral("action"), enumKey(action.type));
    ev.extra.insert(QStringLiteral("ruleId"), action.ruleId.toString(QUuid::WithoutBraces));
    ev.extra.insert(QStringLiteral("ruleName"), action.ruleName);
    ev.extra.insert(QStringLiteral("target"), target);
    ev.extra.insert(QStringLiteral("reason"), action.reason);
    ev.extra.insert(QStringLiteral("deadline"), action.deadline.toString(Qt::ISODate));
    ev.extra.insert(QStringLiteral("countdownSec"), action.remainingSec(now));
    ev.extra.insert(QStringLiteral("cancellable"), action.cancellable);
    ev.extra.insert(QStringLiteral("fsd"), action.fsd);
    ev.extra.insert(QStringLiteral("dryRun"), action.dryRun);
    if (!detail.isEmpty())
        ev.extra.insert(QStringLiteral("detail"), detail);

    if (m_history) {
        if (auto* e = entry(action.deviceId); e && e->episodeId)
            m_history->appendEpisodeAction(e->episodeId, now, ev.title);
    }
    recordEvent(ev);
}

std::optional<PendingAction> Monitor::pendingAction() const
{
    return m_arbiter.pending();
}

bool Monitor::cancelPendingAction()
{
    const auto cancelled = m_arbiter.cancel();
    if (!cancelled)
        return false;
    raiseActionEvent(EventType::ActionCancelled, *cancelled,
        QCoreApplication::translate("nutty::Events", "cancelled by the user"));
    emit pendingActionChanged();
    return true;
}

void Monitor::snoozeActions(int seconds)
{
    if (seconds <= 0) {
        clearSnooze();
        return;
    }
    const auto now = this->now();
    m_arbiter.snooze(now.addSecs(seconds));
    if (const auto& p = m_arbiter.pending(); p && p->cancellable && !p->dryRun) {
        const auto cancelled = m_arbiter.cancel();
        // Not marked as fired any more: if the condition still holds when the
        // snooze ends, the rule asks again.
        m_rules.unmarkFired(cancelled->ruleId);
        raiseActionEvent(EventType::ActionCancelled, *cancelled,
            QCoreApplication::translate("nutty::Events", "snoozed for %1").arg(text::duration(seconds)));
        emit pendingActionChanged();
    }
    emit snoozeChanged();
}

void Monitor::clearSnooze()
{
    m_arbiter.clearSnooze();
    emit snoozeChanged();
    evaluateRules();
}

QDateTime Monitor::snoozedUntil() const
{
    return m_arbiter.isSnoozed(now()) ? m_arbiter.snoozedUntil() : QDateTime {};
}

void Monitor::setDryRun(bool enabled)
{
    m_dryRun = enabled;
}

bool Monitor::simulateRule(const QUuid& ruleId)
{
    const auto it = std::ranges::find(m_config.rules, ruleId, &Rule::id);
    if (it == m_config.rules.end())
        return false;
    const auto now = this->now();

    RuleTrigger t;
    t.rule = *it;
    t.fsd = it->condition == ConditionType::ForcedShutdown;
    t.reason = QCoreApplication::translate("nutty::Events", "simulation");
    QUuid deviceId = it->target;
    if (it->scope == RuleScope::Device) {
        if (const auto* d = m_config.device(deviceId))
            t.targetName = text::deviceName(*d);
    } else if (const auto* g = m_config.group(it->target)) {
        t.targetName = g->name.trimmed().isEmpty()
            ? QCoreApplication::translate("nutty::Events", "Unnamed group")
            : g->name.trimmed();
        deviceId = g->members.value(0);
    }
    t.deviceId = deviceId;
    if (const auto* d = m_config.device(deviceId))
        t.deviceName = text::deviceName(*d);
    if (const auto* s = m_detector.state(deviceId); s && s->hasData)
        t.runtime = s->status.runtime;

    if (it->action == ActionType::Notify) {
        PendingAction p = m_arbiter.makePending(t, now);
        p.dryRun = true;
        raiseActionEvent(EventType::ActionExecuted, p);
        return true;
    }

    auto candidate = m_arbiter.makePending(t, now);
    candidate.dryRun = true;
    candidate.cancellable = true;  // even a simulated FSD can be stopped
    candidate.fsd = false;
    candidate.episodeKey.clear();
    const auto result = m_arbiter.offer(candidate, now);
    if (result != ActionArbiter::Offer::Accepted && result != ActionArbiter::Offer::Escalated)
        return false;
    raiseActionEvent(EventType::ActionScheduled, *m_arbiter.pending());
    emit pendingActionChanged();
    return true;
}

void Monitor::reportActionResult(const PendingAction& action, bool ok, const QString& error)
{
    raiseActionEvent(ok ? EventType::ActionExecuted : EventType::ActionFailed, action, error);
}

void Monitor::prepareSourcesForShutdown()
{
    for (auto& [id, e] : m_entries) {
        if (e.source)
            e.source->prepareForShutdown();
    }
    if (m_history)
        m_history->flush();
}

// --- sleep/wake -------------------------------------------------------------

void Monitor::notifySleeping()
{
    m_sleeping = true;
    if (m_history)
        m_history->flush();
}

void Monitor::notifyResumed()
{
    const auto now = this->now();
    m_sleeping = false;
    m_wakeGraceUntil = now.addSecs(std::max(0, m_config.actions.wakeGraceSec));
    m_detector.markResumed(now);

    // A countdown from before the sleep is based on old data: drop it, and let
    // its rule fire again once fresh data confirms the condition.
    if (const auto& p = m_arbiter.pending()) {
        const auto dropped = *p;
        m_arbiter.clear();
        m_rules.unmarkFired(dropped.ruleId);
        raiseActionEvent(EventType::ActionCancelled, dropped,
            QCoreApplication::translate("nutty::Events", "the computer was asleep"));
        emit pendingActionChanged();
    }

    for (auto& [id, e] : m_entries) {
        if (e.source)
            e.source->reset();
    }
}

}  // namespace nutty
