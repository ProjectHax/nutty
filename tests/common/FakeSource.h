// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/DeviceStatus.h"
#include "core/model/Source.h"

#include <QDateTime>
#include <QList>
#include <QPair>
#include <QTimeZone>
#include <QTimer>

#include <memory>
#include <optional>

// Test doubles shared by the engine tests. Header-only and without Q_OBJECT,
// so tests can include it without extra moc steps.
namespace nutty::test {

// A Source the test drives by hand: push() delivers a status, fail() an error.
class FakeSource : public Source
{
public:
    explicit FakeSource(DeviceConfig config, QObject* parent = nullptr)
        : Source(std::move(config), parent)
    {
        instances().append(this);
    }
    ~FakeSource() override { instances().removeAll(this); }

    void start() override
    {
        ++starts;
        setConnectionState(ConnectionState::Connecting);
    }
    void stop() override
    {
        ++stops;
        setConnectionState(ConnectionState::Disconnected);
    }
    void pollNow() override { ++polls; }
    void reset() override
    {
        ++resets;
        Source::reset();
    }
    void prepareForShutdown() override { ++shutdownPreparations; }
    QString describe() const override { return QStringLiteral("fake"); }

    void push(DeviceStatus status) { publish(std::move(status)); }
    void fail(SourceError kind, const QString& message) { reportError(kind, message); }

    // Settings: `settings` is what controls() reports; applyControl() records
    // the change (or refuses with `refusal`) and finishControl() ends it.
    DeviceDetails details() const override { return fakeDetails; }
    QList<DeviceControl> controls() const override { return settings; }
    bool applyControl(const QString& id, double value, QString* error = nullptr) override
    {
        if (!refusal.isEmpty()) {
            if (error)
                *error = refusal;
            return false;
        }
        applied.append({ id, value });
        for (DeviceControl& c : settings) {
            if (c.id == id)
                c.pending = value;
        }
        emit controlsChanged();
        return true;
    }
    void finishControl(const QString& id, double value, ControlResult result, const QString& error = {})
    {
        for (DeviceControl& c : settings) {
            if (c.id == id) {
                c.pending.reset();
                if (result == ControlResult::Applied)
                    c.value = value;
            }
        }
        emit controlsChanged();
        emit controlFinished(id, value, result, error);
    }

    int starts = 0;
    int stops = 0;
    int polls = 0;
    int resets = 0;
    int shutdownPreparations = 0;
    QList<DeviceControl> settings;
    DeviceDetails fakeDetails;
    QString refusal;
    QList<QPair<QString, double>> applied;

    // Every live FakeSource, oldest first.
    static QList<FakeSource*>& instances()
    {
        static QList<FakeSource*> list;
        return list;
    }
    static FakeSource* find(const QUuid& device)
    {
        for (auto* s : instances()) {
            if (s->deviceId() == device)
                return s;
        }
        return nullptr;
    }
    // Makes SourceRegistry create FakeSources for `type`.
    static void registerFor(SourceType type)
    {
        SourceRegistry::registerFactory(type, [](const DeviceConfig& config, QObject* parent) {
            return std::make_unique<FakeSource>(config, parent);
        });
    }
};

// A Discovery that finds a fixed list of devices on the next event loop turn.
class FakeDiscovery : public Discovery
{
public:
    FakeDiscovery(QList<DiscoveredDevice> devices, QObject* parent)
        : Discovery(parent)
    {
        QTimer::singleShot(0, this, [this, devices = std::move(devices)] { finish(devices); });
    }
    // Makes Discovery::start() find `devices` for `type`.
    static void registerFor(SourceType type, QList<DiscoveredDevice> devices)
    {
        Discovery::registerFactory(
            type, [devices = std::move(devices)](const QVariantMap&, const QVariantMap&, QObject* parent) {
                return new FakeDiscovery(devices, parent);
            });
    }
};

// A clock the test advances by hand.
struct FakeClock {
    QDateTime now = QDateTime(QDate(2026, 3, 1), QTime(12, 0), QTimeZone::utc());

    void advance(qint64 seconds) { now = now.addSecs(seconds); }
};

inline DeviceStatus makeStatus(StatusFlags flags, std::optional<double> charge = std::nullopt,
    std::optional<qint64> runtime = std::nullopt, const QDateTime& at = {})
{
    DeviceStatus s;
    s.sampledAt = at.isValid() ? at : QDateTime::currentDateTimeUtc();
    s.flags = flags;
    s.charge = charge;
    s.runtime = runtime;
    return s;
}

inline DeviceStatus online(
    std::optional<double> charge = 100, std::optional<qint64> runtime = 3600, const QDateTime& at = {})
{
    return makeStatus(StatusFlags(StatusFlag::Online), charge, runtime, at);
}

inline DeviceStatus onBattery(std::optional<double> charge, std::optional<qint64> runtime,
    const QDateTime& at = {}, StatusFlags extra = {})
{
    return makeStatus(
        StatusFlags(StatusFlag::OnBattery) | StatusFlag::Discharging | extra, charge, runtime, at);
}

inline DeviceConfig makeDevice(
    const QString& name, SourceType type = SourceType::Nut, int pollIntervalSec = 5)
{
    DeviceConfig d;
    d.id = QUuid::createUuid();
    d.name = name;
    d.type = type;
    d.pollIntervalSec = pollIntervalSec;
    return d;
}

inline Rule makeRule(const QUuid& target, ConditionType condition, double threshold, ActionType action,
    int countdownSec = 60, RuleScope scope = RuleScope::Device)
{
    Rule r;
    r.id = QUuid::createUuid();
    r.name = QStringLiteral("rule");
    r.scope = scope;
    r.target = target;
    r.condition = condition;
    r.threshold = threshold;
    r.action = action;
    r.countdownSec = countdownSec;
    return r;
}

}  // namespace nutty::test
