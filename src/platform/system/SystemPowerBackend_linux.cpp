// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/system/SystemPowerBackend.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QMap>
#include <QRegularExpression>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>

namespace nutty {

namespace {

const auto kService = QStringLiteral("org.freedesktop.UPower");
const auto kPath = QStringLiteral("/org/freedesktop/UPower");
const auto kInterface = QStringLiteral("org.freedesktop.UPower");
const auto kDeviceInterface = QStringLiteral("org.freedesktop.UPower.Device");
const auto kPropertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");
constexpr int kCallTimeoutMs = 3000;

// org.freedesktop.UPower.Device enums.
enum UpType : uint { TypeLinePower = 1, TypeBattery = 2, TypeUps = 3 };
enum UpState : uint {
    StateCharging = 1,
    StateDischarging = 2,
    StateEmpty = 3,
    StateFullyCharged = 4,
    StatePendingCharge = 5,
    StatePendingDischarge = 6,
};
enum UpWarning : uint { WarningLow = 3, WarningCritical = 4, WarningAction = 5 };

SystemPowerInfo::State mapState(uint state)
{
    switch (state) {
    case StateCharging:
        return SystemPowerInfo::State::Charging;
    case StateDischarging:
    case StateEmpty:
        return SystemPowerInfo::State::Discharging;
    case StateFullyCharged:
        return SystemPowerInfo::State::Full;
    case StatePendingCharge:
    case StatePendingDischarge:
        return SystemPowerInfo::State::NotCharging;
    default:
        return SystemPowerInfo::State::Unknown;
    }
}

std::optional<double> positive(const QVariant& v)
{
    bool ok = false;
    const double d = v.toDouble(&ok);
    return ok && d > 0 ? std::optional<double>(d) : std::nullopt;
}

std::optional<qint64> positiveSeconds(const QVariant& v)
{
    bool ok = false;
    const qint64 n = v.toLongLong(&ok);
    return ok && n > 0 ? std::optional<qint64>(n) : std::nullopt;
}

// USB UPS units often put their firmware in the product string, e.g.
// "Smart-UPS X 1500 FW:COM 03.8 / UPS 03.8". It has no place in a name
// or the model line.
QString withoutFirmware(QString model)
{
    static const QRegularExpression firmware(
        QStringLiteral("\\s+FW:.*$"), QRegularExpression::CaseInsensitiveOption);
    return model.remove(firmware).trimmed();
}

// UPower through the system bus. Device properties are cached and kept fresh
// by PropertiesChanged plus a GetAll per device on every refresh().
class UPowerBackend : public SystemPowerBackend
{
    Q_OBJECT

public:
    explicit UPowerBackend(QObject* parent)
        : SystemPowerBackend(parent)
        , m_bus(QDBusConnection::systemBus())
    {
        m_changedTimer.setSingleShot(true);
        m_changedTimer.setInterval(100);
        connect(&m_changedTimer, &QTimer::timeout, this, &SystemPowerBackend::changed);

        if (m_bus.isConnected()) {
            m_bus.connect(kService, kPath, kInterface, QStringLiteral("DeviceAdded"), this,
                SLOT(onDeviceAdded(QDBusObjectPath)));
            m_bus.connect(kService, kPath, kInterface, QStringLiteral("DeviceRemoved"), this,
                SLOT(onDeviceRemoved(QDBusObjectPath)));
            // Empty path: any object of the service (the devices and the daemon).
            m_bus.connect(kService, QString(), kPropertiesInterface, QStringLiteral("PropertiesChanged"),
                this, SLOT(onPropertiesChanged(QDBusMessage)));
        }
        enumerate();
    }

    bool isAvailable() const override { return m_available; }
    bool isReady() const override { return m_ready; }
    QString backendName() const override { return QStringLiteral("UPower"); }

    QList<SystemPowerInfo> sources() const override
    {
        std::optional<bool> acOnline;
        for (const auto& props : m_devices) {
            if (props.value(QStringLiteral("Type")).toUInt() == TypeLinePower)
                acOnline = acOnline.value_or(false) || props.value(QStringLiteral("Online")).toBool();
        }
        if (!acOnline && m_onBattery)
            acOnline = !*m_onBattery;

        QList<SystemPowerInfo> out;
        for (auto it = m_devices.cbegin(); it != m_devices.cend(); ++it) {
            const QVariantMap& p = it.value();
            const uint type = p.value(QStringLiteral("Type")).toUInt();
            const bool ups = type == TypeUps;
            if (!ups && !(type == TypeBattery && p.value(QStringLiteral("PowerSupply")).toBool()))
                continue;

            SystemPowerInfo info;
            info.id = it.key().section(u'/', -1);
            info.kind = ups ? SystemPowerInfo::Kind::Ups : SystemPowerInfo::Kind::Battery;
            info.state = mapState(p.value(QStringLiteral("State")).toUInt());
            info.present = p.value(QStringLiteral("IsPresent"), true).toBool();
            info.acOnline = acOnline;
            if (info.present) {
                bool ok = false;
                const double pct = p.value(QStringLiteral("Percentage")).toDouble(&ok);
                if (ok)
                    info.charge = std::clamp(pct, 0.0, 100.0);
            }
            info.timeToEmpty = positiveSeconds(p.value(QStringLiteral("TimeToEmpty")));
            info.timeToFull = positiveSeconds(p.value(QStringLiteral("TimeToFull")));
            info.watts = positive(p.value(QStringLiteral("EnergyRate")));
            info.voltage = positive(p.value(QStringLiteral("Voltage")));
            // UPower reports 0 when the temperature is unknown.
            if (const double t = p.value(QStringLiteral("Temperature")).toDouble(); t != 0.0)
                info.temperature = t;
            const uint warning = p.value(QStringLiteral("WarningLevel")).toUInt();
            info.low = warning >= WarningLow && warning <= WarningAction;
            info.critical = warning == WarningCritical || warning == WarningAction;
            info.vendor = p.value(QStringLiteral("Vendor")).toString().trimmed();
            info.model = withoutFirmware(p.value(QStringLiteral("Model")).toString());
            info.serial = p.value(QStringLiteral("Serial")).toString().trimmed();

            const QString product = QStringList { info.vendor, info.model }.join(u' ').trimmed();
            if (!product.isEmpty())
                info.name = product;
            else if (const QString native = p.value(QStringLiteral("NativePath")).toString();
                !native.isEmpty())
                info.name = (ups ? tr("UPS %1") : tr("Battery %1")).arg(native.section(u'/', -1));
            else
                info.name = ups ? tr("UPS") : tr("Battery");
            out << info;
        }
        return out;
    }

    void refresh() override
    {
        if (m_pending > 0)
            return;  // answers are on the way; changed() follows them
        if (!m_available) {
            enumerate();  // UPower may have started since
            return;
        }
        ++m_pending;
        fetchOnBattery();
        for (auto it = m_devices.cbegin(); it != m_devices.cend(); ++it)
            fetch(it.key());
        settle();
    }

private slots:
    void onDeviceAdded(const QDBusObjectPath& path)
    {
        if (!m_available || m_devices.contains(path.path()))
            return;
        m_devices.insert(path.path(), {});
        ++m_pending;
        fetch(path.path());
        settle();
    }

    void onDeviceRemoved(const QDBusObjectPath& path)
    {
        if (m_devices.remove(path.path()))
            m_changedTimer.start();
    }

    void onPropertiesChanged(const QDBusMessage& message)
    {
        const QList<QVariant> args = message.arguments();
        if (args.size() < 2)
            return;
        const QString iface = args.at(0).toString();
        const QVariantMap changed = qdbus_cast<QVariantMap>(args.at(1));
        const QString path = message.path();

        if (path == kPath && iface == kInterface) {
            if (changed.contains(QStringLiteral("OnBattery"))) {
                m_onBattery = changed.value(QStringLiteral("OnBattery")).toBool();
                m_changedTimer.start();
            }
            return;
        }
        if (iface != kDeviceInterface)
            return;
        const auto it = m_devices.find(path);
        if (it == m_devices.end())
            return;
        for (auto c = changed.cbegin(); c != changed.cend(); ++c)
            it->insert(c.key(), c.value());
        if (args.size() >= 3 && !args.at(2).toStringList().isEmpty()) {
            ++m_pending;
            fetch(path);
            settle();
        }
        m_changedTimer.start();
    }

private:
    void enumerate()
    {
        if (!m_bus.isConnected()) {
            m_available = false;
            markReady();
            QTimer::singleShot(0, this, &SystemPowerBackend::changed);
            return;
        }
        ++m_pending;
        const auto call
            = QDBusMessage::createMethodCall(kService, kPath, kInterface, QStringLiteral("EnumerateDevices"));
        auto* watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, kCallTimeoutMs), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            const QDBusPendingReply<QList<QDBusObjectPath>> reply = *w;
            if (reply.isError()) {
                m_available = false;
                m_devices.clear();
                settle();
                return;
            }
            m_available = true;
            QMap<QString, QVariantMap> devices;
            for (const QDBusObjectPath& p : reply.value())
                devices.insert(p.path(), m_devices.value(p.path()));
            m_devices = std::move(devices);
            // The EnumerateDevices request stays counted until every fetch is queued.
            fetchOnBattery();
            for (auto it = m_devices.cbegin(); it != m_devices.cend(); ++it)
                fetch(it.key());
            settle();
        });
    }

    void fetch(const QString& path)
    {
        ++m_pending;
        auto call
            = QDBusMessage::createMethodCall(kService, path, kPropertiesInterface, QStringLiteral("GetAll"));
        call << kDeviceInterface;
        auto* watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, kCallTimeoutMs), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, path](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            const QDBusPendingReply<QVariantMap> reply = *w;
            const auto it = m_devices.find(path);
            if (!reply.isError() && it != m_devices.end())
                *it = reply.value();
            settle();
        });
    }

    void fetchOnBattery()
    {
        ++m_pending;
        auto call
            = QDBusMessage::createMethodCall(kService, kPath, kPropertiesInterface, QStringLiteral("Get"));
        call << kInterface << QStringLiteral("OnBattery");
        auto* watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, kCallTimeoutMs), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            const QDBusPendingReply<QDBusVariant> reply = *w;
            if (!reply.isError())
                m_onBattery = reply.value().variant().toBool();
            settle();
        });
    }

    // One outstanding request finished; when none are left the cache is
    // complete and listeners hear about it.
    void settle()
    {
        if (--m_pending > 0)
            return;
        m_pending = 0;
        markReady();
        m_changedTimer.stop();
        emit changed();
    }

    void markReady()
    {
        if (m_ready)
            return;
        m_ready = true;
        emit ready();
    }

    QDBusConnection m_bus;
    QMap<QString, QVariantMap> m_devices;  // object path -> UPower.Device properties
    std::optional<bool> m_onBattery;
    QTimer m_changedTimer;
    int m_pending = 0;
    bool m_available = false;
    bool m_ready = false;
};

}  // namespace

SystemPowerBackend* SystemPowerBackend::create(QObject* parent)
{
    return new UPowerBackend(parent);
}

}  // namespace nutty

#include "SystemPowerBackend_linux.moc"
