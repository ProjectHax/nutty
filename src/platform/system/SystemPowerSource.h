// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Source.h"
#include "platform/system/SystemPowerBackend.h"

#include <QPointer>
#include <QTimer>

namespace nutty {

// Monitors one of the OS's own power sources: a UPS the OS manages directly
// (USB HID) or the laptop battery. keys::SystemSourceId picks the source;
// empty means the first UPS, else the first battery.
class SystemPowerSource : public Source
{
    Q_OBJECT

public:
    explicit SystemPowerSource(DeviceConfig config, QObject* parent = nullptr);
    // Uses `backend` instead of the OS one and takes ownership (tests).
    SystemPowerSource(DeviceConfig config, SystemPowerBackend* backend, QObject* parent = nullptr);
    ~SystemPowerSource() override;

    void start() override;
    void stop() override;
    void pollNow() override;
    QString describe() const override;

    // The source `id` selects, nullptr if none matches.
    static const SystemPowerInfo* select(const QList<SystemPowerInfo>& sources, const QString& id);
    static DeviceStatus toStatus(const SystemPowerInfo& info);

private:
    void update();

    QPointer<SystemPowerBackend> m_backend;
    QTimer m_pollTimer;
    QString m_sourceId;
    QString m_name;
    bool m_running = false;
};

class SystemDiscovery : public Discovery
{
    Q_OBJECT

public:
    explicit SystemDiscovery(QObject* parent = nullptr);
    // Takes ownership of `backend` (tests).
    explicit SystemDiscovery(SystemPowerBackend* backend, QObject* parent = nullptr);

private:
    void begin(SystemPowerBackend* backend);
    void report();

    SystemPowerBackend* m_backend = nullptr;
    bool m_done = false;
};

}  // namespace nutty
