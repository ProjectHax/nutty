// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/system/SystemPowerBackend.h"

#include <QTimer>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/ps/IOPSKeys.h>
#include <IOKit/ps/IOPowerSources.h>
#include <algorithm>
#include <cmath>

namespace nutty {

namespace {

CFTypeRef value(CFDictionaryRef dict, const char* key)
{
    CFStringRef cfKey = CFStringCreateWithCString(kCFAllocatorDefault, key, kCFStringEncodingUTF8);
    CFTypeRef v = CFDictionaryGetValue(dict, cfKey);
    CFRelease(cfKey);
    return v;
}

QString stringValue(CFDictionaryRef dict, const char* key)
{
    CFTypeRef v = value(dict, key);
    if (!v || CFGetTypeID(v) != CFStringGetTypeID())
        return {};
    return QString::fromCFString(static_cast<CFStringRef>(v)).trimmed();
}

std::optional<double> numberValue(CFDictionaryRef dict, const char* key)
{
    CFTypeRef v = value(dict, key);
    if (!v || CFGetTypeID(v) != CFNumberGetTypeID())
        return std::nullopt;
    double d = 0;
    if (!CFNumberGetValue(static_cast<CFNumberRef>(v), kCFNumberDoubleType, &d))
        return std::nullopt;
    return d;
}

std::optional<bool> boolValue(CFDictionaryRef dict, const char* key)
{
    CFTypeRef v = value(dict, key);
    if (!v || CFGetTypeID(v) != CFBooleanGetTypeID())
        return std::nullopt;
    return CFBooleanGetValue(static_cast<CFBooleanRef>(v)) != 0;
}

// The IOKit power-source list: the internal battery and any UPS that macOS
// manages over USB HID. IOPSNotificationCreateRunLoopSource() reports changes
// on the main run loop, which Qt's event dispatcher drives.
class IOKitPowerBackend : public SystemPowerBackend
{
public:
    explicit IOKitPowerBackend(QObject* parent)
        : SystemPowerBackend(parent)
    {
        read();
        m_runLoopSource = IOPSNotificationCreateRunLoopSource(&IOKitPowerBackend::onNotification, this);
        if (m_runLoopSource)
            CFRunLoopAddSource(CFRunLoopGetMain(), m_runLoopSource, kCFRunLoopCommonModes);
    }

    ~IOKitPowerBackend() override
    {
        if (m_runLoopSource) {
            CFRunLoopRemoveSource(CFRunLoopGetMain(), m_runLoopSource, kCFRunLoopCommonModes);
            CFRelease(m_runLoopSource);
        }
    }

    bool isAvailable() const override { return m_available; }
    bool isReady() const override { return true; }
    QList<SystemPowerInfo> sources() const override { return m_sources; }
    QString backendName() const override { return QStringLiteral("IOKit"); }

    void refresh() override
    {
        read();
        QTimer::singleShot(0, this, &SystemPowerBackend::changed);
    }

private:
    static void onNotification(void* context)
    {
        auto* self = static_cast<IOKitPowerBackend*>(context);
        self->read();
        emit self->changed();
    }

    void read()
    {
        m_sources.clear();
        CFTypeRef blob = IOPSCopyPowerSourcesInfo();
        m_available = blob != nullptr;
        if (!blob)
            return;
        CFArrayRef list = IOPSCopyPowerSourcesList(blob);
        if (!list) {
            CFRelease(blob);
            return;
        }

        std::optional<bool> acOnline;
        if (CFStringRef providing = IOPSGetProvidingPowerSourceType(blob)) {
            const QString p = QString::fromCFString(providing);
            acOnline = p == u"AC Power";
        }
        const IOPSLowBatteryWarningLevel systemWarning = IOPSGetBatteryWarningLevel();

        for (CFIndex i = 0; i < CFArrayGetCount(list); ++i) {
            CFDictionaryRef desc = IOPSGetPowerSourceDescription(blob, CFArrayGetValueAtIndex(list, i));
            if (!desc)
                continue;
            const QString type = stringValue(desc, kIOPSTypeKey);
            const bool ups = type == QLatin1StringView(kIOPSUPSType);
            if (!ups && type != QLatin1StringView(kIOPSInternalBatteryType))
                continue;

            SystemPowerInfo info;
            info.kind = ups ? SystemPowerInfo::Kind::Ups : SystemPowerInfo::Kind::Battery;
            info.name = stringValue(desc, kIOPSNameKey);
            if (info.name.isEmpty())
                info.name = ups ? SystemPowerBackend::tr("UPS") : SystemPowerBackend::tr("Battery");
            info.id = info.name;
            info.present = boolValue(desc, kIOPSIsPresentKey).value_or(true);
            info.acOnline = acOnline;
            info.serial = stringValue(desc, kIOPSHardwareSerialNumberKey);

            const auto current = numberValue(desc, kIOPSCurrentCapacityKey);
            const auto max = numberValue(desc, kIOPSMaxCapacityKey);
            if (current && max && *max > 0)
                info.charge = std::clamp(*current * 100.0 / *max, 0.0, 100.0);

            const QString state = stringValue(desc, kIOPSPowerSourceStateKey);
            const bool charging = boolValue(desc, kIOPSIsChargingKey).value_or(false);
            const bool charged = boolValue(desc, kIOPSIsChargedKey).value_or(false);
            if (state == QLatin1StringView(kIOPSBatteryPowerValue))
                info.state = SystemPowerInfo::State::Discharging;
            else if (charging)
                info.state = SystemPowerInfo::State::Charging;
            else if (charged || (info.charge && *info.charge >= 100))
                info.state = SystemPowerInfo::State::Full;
            else if (state == QLatin1StringView(kIOPSACPowerValue))
                info.state = SystemPowerInfo::State::NotCharging;

            // Minutes; -1 while macOS is still estimating.
            if (const auto tte = numberValue(desc, kIOPSTimeToEmptyKey); tte && *tte > 0)
                info.timeToEmpty = static_cast<qint64>(*tte * 60);
            if (const auto ttf = numberValue(desc, kIOPSTimeToFullChargeKey); ttf && *ttf > 0)
                info.timeToFull = static_cast<qint64>(*ttf * 60);

            // mV and mA.
            const auto mv = numberValue(desc, kIOPSVoltageKey);
            const auto ma = numberValue(desc, kIOPSCurrentKey);
            if (mv && *mv > 0)
                info.voltage = *mv / 1000.0;
            if (mv && ma && *mv > 0 && *ma != 0)
                info.watts = std::abs(*mv * *ma) / 1e6;

            if (ups) {
                // A UPS carries its own thresholds, in percent.
                const auto lowLevel = numberValue(desc, kIOPSLowWarnLevelKey);
                const auto deadLevel = numberValue(desc, kIOPSDeadWarnLevelKey);
                info.critical = info.charge && deadLevel && *info.charge <= *deadLevel;
                info.low = info.critical || (info.charge && lowLevel && *info.charge <= *lowLevel);
            } else {
                info.low = systemWarning == kIOPSLowBatteryWarningEarly
                    || systemWarning == kIOPSLowBatteryWarningFinal;
                info.critical = systemWarning == kIOPSLowBatteryWarningFinal;
            }
            m_sources << info;
        }
        CFRelease(list);
        CFRelease(blob);
    }

    QList<SystemPowerInfo> m_sources;
    CFRunLoopSourceRef m_runLoopSource = nullptr;
    bool m_available = false;
};

}  // namespace

SystemPowerBackend* SystemPowerBackend::create(QObject* parent)
{
    return new IOKitPowerBackend(parent);
}

}  // namespace nutty
