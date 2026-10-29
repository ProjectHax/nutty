// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/system/SystemPowerSource.h"

namespace nutty {

namespace {

constexpr int kDiscoveryTimeoutMs = 5000;

QString stateKey(SystemPowerInfo::State state)
{
    switch (state) {
    case SystemPowerInfo::State::Charging:
        return QStringLiteral("charging");
    case SystemPowerInfo::State::Discharging:
        return QStringLiteral("discharging");
    case SystemPowerInfo::State::Full:
        return QStringLiteral("full");
    case SystemPowerInfo::State::NotCharging:
        return QStringLiteral("not-charging");
    case SystemPowerInfo::State::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

}  // namespace

SystemPowerSource::SystemPowerSource(DeviceConfig config, QObject* parent)
    : SystemPowerSource(std::move(config), nullptr, parent)
{
}

SystemPowerSource::SystemPowerSource(DeviceConfig config, SystemPowerBackend* backend, QObject* parent)
    : Source(std::move(config), parent)
{
    m_sourceId = this->config().settings.value(QLatin1StringView(keys::SystemSourceId)).toString().trimmed();
    if (backend)
        backend->setParent(this);
    else
        backend = SystemPowerBackend::create(this);
    m_backend = backend;
    connect(backend, &SystemPowerBackend::ready, this, &SystemPowerSource::update);
    connect(backend, &SystemPowerBackend::changed, this, &SystemPowerSource::update);
    connect(&m_pollTimer, &QTimer::timeout, this, &SystemPowerSource::pollNow);
}

SystemPowerSource::~SystemPowerSource()
{
    m_pollTimer.stop();
}

QString SystemPowerSource::describe() const
{
    return m_name.isEmpty() ? tr("This computer") : m_name;
}

void SystemPowerSource::start()
{
    m_running = true;
    setConnectionState(ConnectionState::Connecting);
    m_pollTimer.start(pollIntervalMs());
    pollNow();
}

void SystemPowerSource::stop()
{
    m_running = false;
    m_pollTimer.stop();
    setConnectionState(ConnectionState::Disconnected);
}

void SystemPowerSource::pollNow()
{
    if (m_running && m_backend)
        m_backend->refresh();
}

void SystemPowerSource::update()
{
    if (!m_running || !m_backend || !m_backend->isReady())
        return;
    if (!m_backend->isAvailable()) {
        reportError(
            SourceError::NotFound, tr("%1 is not available on this system").arg(m_backend->backendName()));
        return;
    }
    const QList<SystemPowerInfo> sources = m_backend->sources();
    const SystemPowerInfo* info = select(sources, m_sourceId);
    if (!info) {
        reportError(SourceError::NotFound,
            m_sourceId.isEmpty() ? tr("No battery or UPS was found")
                                 : tr("The power source \"%1\" was not found").arg(m_sourceId));
        return;
    }
    m_name = info->name;
    if (!info->present) {
        reportError(SourceError::NotFound, tr("%1 is not present").arg(info->name));
        return;
    }
    publish(toStatus(*info));
}

const SystemPowerInfo* SystemPowerSource::select(const QList<SystemPowerInfo>& sources, const QString& id)
{
    if (!id.isEmpty()) {
        for (const auto& s : sources) {
            if (s.id == id)
                return &s;
        }
        return nullptr;
    }
    for (const auto& s : sources) {
        if (s.kind == SystemPowerInfo::Kind::Ups)
            return &s;
    }
    return sources.isEmpty() ? nullptr : &sources.front();
}

DeviceStatus SystemPowerSource::toStatus(const SystemPowerInfo& info)
{
    using State = SystemPowerInfo::State;

    DeviceStatus s;
    s.charge = info.charge;
    s.batteryVoltage = info.voltage;
    s.temperature = info.temperature;
    s.manufacturer = info.vendor;
    s.model = info.model.isEmpty() ? info.name : info.model;
    s.serial = info.serial;

    // A UPS's own state says whether it is on battery. A laptop battery can
    // report "discharging" while plugged in (charge thresholds), so the
    // machine's mains state wins when the OS knows it.
    bool onBattery = false;
    if (info.kind == SystemPowerInfo::Kind::Ups)
        onBattery = info.state == State::Discharging;
    else if (info.state == State::Discharging)
        onBattery = info.acOnline != true;
    else if (info.state == State::Unknown)
        onBattery = info.acOnline == false;

    StatusFlags flags;
    if (onBattery) {
        flags |= StatusFlag::OnBattery | StatusFlag::Discharging;
        if (info.timeToEmpty && *info.timeToEmpty > 0)
            s.runtime = info.timeToEmpty;
        s.wattsOut = info.watts;
    } else {
        flags |= StatusFlag::Online;
        if (info.state == State::Charging) {
            flags |= StatusFlag::Charging;
            s.wattsIn = info.watts;
        }
    }
    if (info.low || info.critical)
        flags |= StatusFlag::LowBattery;
    s.flags = flags;

    s.raw.insert(QStringLiteral("system.kind"),
        info.kind == SystemPowerInfo::Kind::Ups ? QStringLiteral("ups") : QStringLiteral("battery"));
    s.raw.insert(QStringLiteral("system.state"), stateKey(info.state));
    if (info.acOnline)
        s.raw.insert(QStringLiteral("system.ac"),
            *info.acOnline ? QStringLiteral("online") : QStringLiteral("offline"));
    if (info.charge)
        s.raw.insert(QStringLiteral("battery.charge"), QString::number(*info.charge, 'f', 1));
    if (info.timeToEmpty)
        s.raw.insert(QStringLiteral("battery.runtime"), QString::number(*info.timeToEmpty));
    if (info.timeToFull)
        s.raw.insert(QStringLiteral("battery.time_to_full"), QString::number(*info.timeToFull));
    if (info.watts)
        s.raw.insert(QStringLiteral("battery.energy_rate"), QString::number(*info.watts, 'f', 2));
    if (info.voltage)
        s.raw.insert(QStringLiteral("battery.voltage"), QString::number(*info.voltage, 'f', 2));
    if (info.temperature)
        s.raw.insert(QStringLiteral("battery.temperature"), QString::number(*info.temperature, 'f', 1));
    if (info.critical)
        s.raw.insert(QStringLiteral("system.warning"), QStringLiteral("critical"));
    else if (info.low)
        s.raw.insert(QStringLiteral("system.warning"), QStringLiteral("low"));
    return s;
}

// --- SystemDiscovery ----------------------------------------------------------

SystemDiscovery::SystemDiscovery(QObject* parent)
    : Discovery(parent)
{
    begin(SystemPowerBackend::create(this));
}

SystemDiscovery::SystemDiscovery(SystemPowerBackend* backend, QObject* parent)
    : Discovery(parent)
{
    backend->setParent(this);
    begin(backend);
}

void SystemDiscovery::begin(SystemPowerBackend* backend)
{
    m_backend = backend;
    if (backend->isReady()) {
        QTimer::singleShot(0, this, &SystemDiscovery::report);
        return;
    }
    connect(backend, &SystemPowerBackend::ready, this, &SystemDiscovery::report);
    QTimer::singleShot(kDiscoveryTimeoutMs, this, &SystemDiscovery::report);
    backend->refresh();
}

void SystemDiscovery::report()
{
    if (m_done)
        return;
    m_done = true;
    if (!m_backend->isReady()) {
        fail(tr("%1 did not respond").arg(m_backend->backendName()));
        return;
    }
    if (!m_backend->isAvailable()) {
        fail(tr("%1 is not available on this system").arg(m_backend->backendName()));
        return;
    }
    QList<DiscoveredDevice> found;
    for (const auto& s : m_backend->sources()) {
        DiscoveredDevice d;
        d.id = s.id;
        d.name = s.name;
        const QString kind = s.kind == SystemPowerInfo::Kind::Ups ? tr("UPS") : tr("Battery");
        d.description
            = s.model.isEmpty() || s.model == s.name ? kind : QStringLiteral("%1 · %2").arg(kind, s.model);
        d.online = s.present;
        found << d;
    }
    finish(found);
}

}  // namespace nutty
