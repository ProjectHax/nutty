// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/system/SystemPowerBackend.h"

#include <QTimer>

#include <windows.h>
// powrprof.h relies on the types from windows.h.
#include <powrprof.h>

namespace nutty {

namespace {

constexpr BYTE kFlagLow = 2;
constexpr BYTE kFlagCritical = 4;
constexpr BYTE kFlagCharging = 8;
constexpr BYTE kFlagNoBattery = 128;
constexpr BYTE kUnknown = 255;

// GetSystemPowerStatus() reports one composite battery. Windows also exposes
// a USB HID UPS that way, so a desktop (no lid) with a battery is treated as
// a UPS.
class Win32PowerBackend : public SystemPowerBackend
{
public:
    explicit Win32PowerBackend(QObject* parent)
        : SystemPowerBackend(parent)
    {
        SYSTEM_POWER_CAPABILITIES caps {};
        if (GetPwrCapabilities(&caps))
            m_desktopUps = caps.SystemBatteriesPresent && !caps.LidPresent;
        read();
    }

    bool isAvailable() const override { return m_available; }
    bool isReady() const override { return true; }
    QList<SystemPowerInfo> sources() const override { return m_sources; }
    QString backendName() const override { return QStringLiteral("Windows power status"); }

    void refresh() override
    {
        read();
        QTimer::singleShot(0, this, &SystemPowerBackend::changed);
    }

private:
    void read()
    {
        m_sources.clear();
        SYSTEM_POWER_STATUS st {};
        m_available = GetSystemPowerStatus(&st) != 0;
        if (!m_available || st.BatteryFlag == kUnknown || (st.BatteryFlag & kFlagNoBattery))
            return;

        SystemPowerInfo info;
        info.id = QStringLiteral("battery");
        info.kind = m_desktopUps ? SystemPowerInfo::Kind::Ups : SystemPowerInfo::Kind::Battery;
        info.name = m_desktopUps ? SystemPowerBackend::tr("UPS") : SystemPowerBackend::tr("Battery");
        if (st.ACLineStatus == 0)
            info.acOnline = false;
        else if (st.ACLineStatus == 1)
            info.acOnline = true;
        if (st.BatteryLifePercent <= 100)
            info.charge = st.BatteryLifePercent;
        if (st.BatteryLifeTime != static_cast<DWORD>(-1) && st.BatteryLifeTime > 0)
            info.timeToEmpty = static_cast<qint64>(st.BatteryLifeTime);

        if (st.BatteryFlag & kFlagCharging)
            info.state = SystemPowerInfo::State::Charging;
        else if (info.acOnline == false)
            info.state = SystemPowerInfo::State::Discharging;
        else if (info.charge && *info.charge >= 100)
            info.state = SystemPowerInfo::State::Full;
        else if (info.acOnline == true)
            info.state = SystemPowerInfo::State::NotCharging;

        info.low = (st.BatteryFlag & (kFlagLow | kFlagCritical)) != 0;
        info.critical = (st.BatteryFlag & kFlagCritical) != 0;
        m_sources << info;
    }

    QList<SystemPowerInfo> m_sources;
    bool m_available = false;
    bool m_desktopUps = false;
};

}  // namespace

SystemPowerBackend* SystemPowerBackend::create(QObject* parent)
{
    return new Win32PowerBackend(parent);
}

}  // namespace nutty
