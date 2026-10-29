// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "app/viewmodels/DeviceEditor.h"

#include "app/viewmodels/AppContext.h"
#include "app/viewmodels/DeviceInfo.h"
#include "app/viewmodels/FieldSpecs.h"
#include "core/engine/EventText.h"
#include "core/model/Source.h"

#include <QLocale>

#include <algorithm>

namespace nutty {

namespace {

QVariant valueOf(const DeviceConfig& d, const QString& key)
{
    return d.secrets.contains(key) ? d.secrets.value(key) : d.settings.value(key);
}

// Whether a field is shown for `d`, given its showIf condition.
bool visibleFor(const QVariantMap& field, const DeviceConfig& d)
{
    const QVariantMap condition = field.value(QStringLiteral("showIf")).toMap();
    for (auto it = condition.cbegin(); it != condition.cend(); ++it) {
        if (valueOf(d, it.key()).toString() != it.value().toString())
            return false;
    }
    return true;
}

// The key of the field the "Find devices" picker fills.
QString discoverKey(SourceType type)
{
    for (const QVariant& f : fields::forSource(type)) {
        const QVariantMap m = f.toMap();
        if (m.value(QStringLiteral("discover")).toBool())
            return m.value(QStringLiteral("key")).toString();
    }
    return {};
}

// The account details `d` uses, as strings with the defaults filled in, so
// that two devices on the same account compare equal.
QVariantMap accountOf(const DeviceConfig& d)
{
    const QVariantMap defaults = fields::sourceDefaults(d.type);
    QVariantMap out;
    for (const QVariant& f : fields::forSource(d.type)) {
        const QVariantMap m = f.toMap();
        if (!m.value(QStringLiteral("account")).toBool() || !visibleFor(m, d))
            continue;
        const QString key = m.value(QStringLiteral("key")).toString();
        const QVariant v = valueOf(d, key);
        out.insert(key, (v.isValid() ? v : defaults.value(key)).toString());
    }
    return out;
}

// Whether `d` has every required account detail (or its default), so that
// another device can use them.
bool hasAccount(const DeviceConfig& d)
{
    const QVariantMap account = accountOf(d);
    for (const QVariant& f : fields::forSource(d.type)) {
        const QVariantMap m = f.toMap();
        const QString key = m.value(QStringLiteral("key")).toString();
        if (m.value(QStringLiteral("required")).toBool() && account.contains(key)
            && account.value(key).toString().trimmed().isEmpty())
            return false;
    }
    return true;
}

// Copies every account detail, shown or not, from one device to another
// (the default where `from` has none).
void copyAccount(const DeviceConfig& from, DeviceConfig& to)
{
    const QVariantMap defaults = fields::sourceDefaults(to.type);
    const QStringList secret = secretKeys(to.type);
    for (const QString& key : fields::accountKeys(to.type)) {
        QVariantMap& target = secret.contains(key) ? to.secrets : to.settings;
        const QVariant v = valueOf(from, key);
        if (v.isValid())
            target.insert(key, v);
        else if (defaults.contains(key))
            target.insert(key, defaults.value(key));
        else
            target.remove(key);
    }
}

}  // namespace

DeviceEditor::DeviceEditor(QObject* parent)
    : DeviceEditor(AppContext::instance(), parent)
{
}

DeviceEditor::DeviceEditor(AppContext* context, QObject* parent)
    : QObject(parent)
    , m_context(context)
{
    m_testTimer.setSingleShot(true);
    m_testTimer.setInterval(10000);
    connect(&m_testTimer, &QTimer::timeout, this,
        [this] { finishTest(false, tr("No answer within %1 seconds.").arg(m_testTimer.interval() / 1000)); });
    reset(QStringLiteral("Nut"));
}

DeviceEditor::~DeviceEditor()
{
    delete m_testSource;
}

QString DeviceEditor::deviceId() const
{
    return idString(m_device.id);
}

QString DeviceEditor::type() const
{
    return enumKey(m_device.type);
}

void DeviceEditor::setType(const QString& type)
{
    const SourceType t = enumFromKey(type, m_device.type);
    if (t == m_device.type)
        return;
    m_device.type = t;
    // Keep what still applies (host, port) and add the new type's defaults.
    const QVariantMap defaults = fields::sourceDefaults(t);
    for (auto it = defaults.cbegin(); it != defaults.cend(); ++it) {
        if (!m_device.settings.contains(it.key()))
            m_device.settings.insert(it.key(), it.value());
    }
    m_device.commLostCritical = defaultCommLostCritical(t);
    m_discovered.clear();
    m_discoveryError.clear();
    emit discoveryChanged();
    emit typeChanged();
    emit valuesChanged();
    markEdited();
}

QString DeviceEditor::typeName() const
{
    return deviceinfo::typeName(m_device.type);
}

QVariantList DeviceEditor::fields() const
{
    return fields::forSource(m_device.type);
}

QVariantMap DeviceEditor::values() const
{
    QVariantMap map = m_device.settings;
    for (auto it = m_device.secrets.cbegin(); it != m_device.secrets.cend(); ++it)
        map.insert(it.key(), it.value());
    return map;
}

QString DeviceEditor::accountNote() const
{
    if (m_isNew && !m_reusedFrom.isEmpty()) {
        switch (m_device.type) {
        case SourceType::Nut:
            return tr("Using the server and login from “%1”.").arg(m_reusedFrom);
        case SourceType::EcoFlow:
            return tr("Using the keys from “%1”.").arg(m_reusedFrom);
        case SourceType::HttpJson:
            return tr("Using the server and token from “%1”.").arg(m_reusedFrom);
        case SourceType::Apcupsd:
        case SourceType::System:
            break;
        }
    }
    if (!m_isNew && !m_sharedWith.isEmpty()) {
        QStringList quoted;
        for (const QString& name : m_sharedWith) {
            //: A device name in quotes, in a list of them ("“Shed” and “Garage”").
            quoted << tr("“%1”").arg(name);
        }
        const QString names = QLocale().createSeparatedList(quoted);
        switch (m_device.type) {
        case SourceType::Nut:
            return tr("Uses the same server and login as %1.").arg(names);
        case SourceType::EcoFlow:
            return tr("Uses the same keys as %1.").arg(names);
        case SourceType::HttpJson:
            return tr("Uses the same server and token as %1.").arg(names);
        case SourceType::Apcupsd:
        case SourceType::System:
            break;
        }
    }
    return {};
}

bool DeviceEditor::accountEdited() const
{
    return !m_isNew && accountOf(m_device) != m_savedAccount;
}

void DeviceEditor::setApplyToShared(bool on)
{
    if (on == m_applyToShared)
        return;
    m_applyToShared = on;
    emit sharingChanged();
}

void DeviceEditor::setName(const QString& name)
{
    if (name == m_device.name)
        return;
    m_device.name = name;
    markEdited();
}

void DeviceEditor::setEnabled(bool enabled)
{
    if (enabled == m_device.enabled)
        return;
    m_device.enabled = enabled;
    markEdited();
}

void DeviceEditor::setPollIntervalSec(int sec)
{
    sec = std::clamp(sec, 0, 3600);
    if (sec == m_device.pollIntervalSec)
        return;
    m_device.pollIntervalSec = sec;
    markEdited();
}

int DeviceEditor::defaultPollIntervalSec() const
{
    return nutty::defaultPollIntervalSec(m_device.type);
}

QString DeviceEditor::kind() const
{
    return enumKey(m_device.kind);
}

void DeviceEditor::setKind(const QString& kind)
{
    const DeviceKind k = enumFromKey(kind, DeviceKind::Auto);
    if (k == m_device.kind)
        return;
    m_device.kind = k;
    markEdited();
}

void DeviceEditor::setCustomImage(const QString& file)
{
    if (file == m_device.customImage)
        return;
    m_device.customImage = file;
    markEdited();
}

QUrl DeviceEditor::imageSource() const
{
    return m_context ? m_context->imageUrl(m_device.customImage) : QUrl();
}

void DeviceEditor::setNotifyChargeBelow(int percent)
{
    percent = std::clamp(percent, 0, 100);
    if (percent == m_device.notifyChargeBelow)
        return;
    m_device.notifyChargeBelow = percent;
    markEdited();
}

void DeviceEditor::setNotifyRuntimeBelowSec(int sec)
{
    sec = std::clamp(sec, 0, 24 * 3600);
    if (sec == m_device.notifyRuntimeBelowSec)
        return;
    m_device.notifyRuntimeBelowSec = sec;
    markEdited();
}

void DeviceEditor::setCommLostCritical(bool on)
{
    if (on == m_device.commLostCritical)
        return;
    m_device.commLostCritical = on;
    markEdited();
}

QStringList DeviceEditor::channels() const
{
    QStringList ids;
    for (const QUuid& id : m_device.channels)
        ids << idString(id);
    return ids;
}

void DeviceEditor::setChannels(const QStringList& ids)
{
    QList<QUuid> list;
    for (const QString& id : ids) {
        const QUuid uuid = parseId(id);
        if (!uuid.isNull() && !list.contains(uuid))
            list << uuid;
    }
    if (list == m_device.channels)
        return;
    m_device.channels = list;
    markEdited();
}

bool DeviceEditor::discoverySupported() const
{
    return Discovery::supports(m_device.type);
}

void DeviceEditor::setTestTimeoutMs(int ms)
{
    ms = std::max(100, ms);
    if (ms == m_testTimer.interval())
        return;
    m_testTimer.setInterval(ms);
    emit testChanged();
}

bool DeviceEditor::load(const QString& id)
{
    const DeviceConfig* device = m_context ? m_context->config().device(parseId(id)) : nullptr;
    if (!device) {
        reset(QStringLiteral("Nut"));
        return false;
    }
    cancelTest();
    cancelDiscovery();
    m_device = *device;
    m_isNew = false;
    m_dirty = false;
    m_discovered.clear();
    m_discoveryError.clear();
    m_testDone = false;
    m_testResult.clear();
    m_reusedFrom.clear();
    m_applyToShared = true;
    updateSharing();
    setError({});
    emit loaded();
    emit typeChanged();
    emit valuesChanged();
    emit edited();
    emit discoveryChanged();
    emit testChanged();
    return true;
}

void DeviceEditor::reset(const QString& type)
{
    cancelTest();
    cancelDiscovery();
    m_device = DeviceConfig {};
    m_device.id = QUuid::createUuid();
    m_device.type = enumFromKey(type, SourceType::Nut);
    m_device.settings = fields::sourceDefaults(m_device.type);
    m_device.commLostCritical = defaultCommLostCritical(m_device.type);
    m_reusedFrom.clear();
    if (m_context && !fields::accountKeys(m_device.type).isEmpty()) {
        const QList<DeviceConfig>& devices = m_context->config().devices;
        for (auto it = devices.crbegin(); it != devices.crend(); ++it) {
            if (it->type == m_device.type && hasAccount(*it)) {
                copyAccount(*it, m_device);
                m_reusedFrom = it->name;
                break;
            }
        }
    }
    m_isNew = true;
    m_dirty = false;
    m_discovered.clear();
    m_discoveryError.clear();
    m_testDone = false;
    m_testResult.clear();
    m_applyToShared = true;
    updateSharing();
    setError({});
    emit loaded();
    emit typeChanged();
    emit valuesChanged();
    emit edited();
    emit discoveryChanged();
    emit testChanged();
}

void DeviceEditor::startBlank()
{
    if (m_reusedFrom.isEmpty())
        return;
    cancelDiscovery();
    DeviceConfig blank;
    blank.type = m_device.type;
    copyAccount(blank, m_device);
    m_reusedFrom.clear();
    m_discovered.clear();
    m_discoveryError.clear();
    updateSharing();
    emit valuesChanged();
    emit discoveryChanged();
}

void DeviceEditor::updateSharing()
{
    m_savedAccount = accountOf(m_device);
    m_sharedWith.clear();
    if (m_context && !m_isNew && !m_savedAccount.isEmpty()) {
        for (const DeviceConfig& other : m_context->config().devices) {
            if (other.id != m_device.id && other.type == m_device.type && accountOf(other) == m_savedAccount)
                m_sharedWith << other.name;
        }
    }
    emit sharingChanged();
}

QVariantMap DeviceEditor::fieldSpec(const QString& key) const
{
    for (const QVariant& f : fields::forSource(m_device.type)) {
        const QVariantMap m = f.toMap();
        if (m.value(QStringLiteral("key")).toString() == key && fieldVisible(m))
            return m;
    }
    for (const QVariant& f : fields::forSource(m_device.type)) {
        const QVariantMap m = f.toMap();
        if (m.value(QStringLiteral("key")).toString() == key)
            return m;
    }
    return {};
}

QVariant DeviceEditor::value(const QString& key) const
{
    if (m_device.secrets.contains(key))
        return m_device.secrets.value(key);
    return m_device.settings.value(key);
}

void DeviceEditor::setValue(const QString& key, const QVariant& value)
{
    if (key.isEmpty())
        return;
    QVariant v = value;
    const QString kind = fieldSpec(key).value(QStringLiteral("kind")).toString();
    if (kind == u"number")
        v = v.toString().trimmed().isEmpty() ? QVariant() : QVariant(v.toInt());
    else if (kind == u"bool")
        v = v.toBool();
    else
        v = v.toString();

    const bool secret = secretKeys(m_device.type).contains(key);
    QVariantMap& target = secret ? m_device.secrets : m_device.settings;
    if (target.value(key) == v)
        return;
    if (v.isNull() || (v.typeId() == QMetaType::QString && v.toString().isEmpty()))
        target.remove(key);
    else
        target.insert(key, v);
    emit valuesChanged();
    markEdited();
}

bool DeviceEditor::fieldVisible(const QVariantMap& field) const
{
    return visibleFor(field, m_device);
}

void DeviceEditor::discover()
{
    if (m_discovery)
        return;
    m_discovered.clear();
    m_discoveryError.clear();
    m_discovery = Discovery::start(m_device.type, m_device.settings, m_device.secrets, this);
    if (!m_discovery) {
        m_discoveryError = tr("This kind of device can't be searched for.");
        emit discoveryChanged();
        return;
    }
    connect(m_discovery, &Discovery::finished, this, [this](const QList<DiscoveredDevice>& devices) {
        m_discovery = nullptr;
        const QString idKey = discoverKey(m_device.type);
        const QVariantMap account = accountOf(m_device);
        const auto alreadyAdded = [&](const DiscoveredDevice& found) {
            if (!m_context)
                return false;
            return std::ranges::any_of(m_context->config().devices, [&](const DeviceConfig& other) {
                return other.id != m_device.id && other.type == m_device.type
                    && valueOf(other, idKey).toString() == found.id && accountOf(other) == account;
            });
        };
        for (const DiscoveredDevice& d : devices) {
            m_discovered.append(QVariantMap {
                { QStringLiteral("id"), d.id },
                { QStringLiteral("name"), d.name },
                { QStringLiteral("description"), d.description },
                { QStringLiteral("online"), d.online },
                { QStringLiteral("added"), alreadyAdded(d) },
            });
        }
        if (devices.isEmpty())
            m_discoveryError = tr("Nothing found there.");
        emit discoveryChanged();
    });
    connect(m_discovery, &Discovery::failed, this, [this](const QString& error) {
        m_discovery = nullptr;
        m_discoveryError = error;
        emit discoveryChanged();
    });
    emit discoveryChanged();
}

void DeviceEditor::cancelDiscovery()
{
    if (!m_discovery)
        return;
    Discovery* discovery = m_discovery;
    m_discovery = nullptr;
    discovery->disconnect(this);
    discovery->deleteLater();
    emit discoveryChanged();
}

void DeviceEditor::pick(int index)
{
    if (index < 0 || index >= m_discovered.size())
        return;
    const QVariantMap d = m_discovered.at(index).toMap();
    for (const QVariant& f : fields::forSource(m_device.type)) {
        const QVariantMap m = f.toMap();
        if (m.value(QStringLiteral("discover")).toBool()) {
            setValue(m.value(QStringLiteral("key")).toString(), d.value(QStringLiteral("id")));
            break;
        }
    }
    if (m_device.name.trimmed().isEmpty())
        setName(d.value(QStringLiteral("name")).toString());
}

DeviceConfig DeviceEditor::current() const
{
    DeviceConfig d = m_device;
    d.name = d.name.trimmed();
    return d;
}

QString DeviceEditor::validate() const
{
    for (const QVariant& f : fields::forSource(m_device.type)) {
        const QVariantMap m = f.toMap();
        if (!m.value(QStringLiteral("required")).toBool() || !fieldVisible(m))
            continue;
        if (value(m.value(QStringLiteral("key")).toString()).toString().trimmed().isEmpty())
            return tr("%1 is required.").arg(m.value(QStringLiteral("label")).toString());
    }
    return {};
}

void DeviceEditor::testConnection()
{
    cancelTest();
    if (const QString problem = validate(); !problem.isEmpty()) {
        m_testDone = true;
        m_testOk = false;
        m_testResult = problem;
        emit testChanged();
        return;
    }
    DeviceConfig config = current();
    config.id = QUuid::createUuid();  // never mistaken for the real device
    config.enabled = true;
    std::unique_ptr<Source> source = SourceRegistry::create(config);
    if (!source) {
        finishTest(false, tr("This kind of device isn't supported on this system."));
        return;
    }
    m_testSource = source.release();
    connect(m_testSource, &Source::statusUpdated, this, [this](const DeviceStatus& status) {
        QStringList parts;
        const QString model = (status.manufacturer + u' ' + status.model).trimmed();
        if (!model.isEmpty())
            parts << model;
        if (const QString s = text::summary(status); !s.isEmpty())
            parts << s;
        parts << (status.onBattery() ? tr("on battery") : status.online() ? tr("on line power") : QString());
        parts.removeAll(QString());
        finishTest(true, tr("Connected: %1").arg(parts.join(QStringLiteral(" · "))));
    });
    connect(m_testSource, &Source::errorOccurred, this, [this](SourceError, const QString& message) {
        finishTest(false, message.isEmpty() ? tr("The connection failed.") : message);
    });
    m_testDone = false;
    m_testResult.clear();
    m_testTimer.start();
    emit testChanged();
    m_testSource->start();
}

void DeviceEditor::cancelTest()
{
    m_testTimer.stop();
    if (!m_testSource)
        return;
    Source* source = m_testSource;
    m_testSource = nullptr;
    source->disconnect(this);
    source->stop();
    source->deleteLater();
    emit testChanged();
}

void DeviceEditor::finishTest(bool ok, const QString& result)
{
    cancelTest();
    m_testDone = true;
    m_testOk = ok;
    m_testResult = result;
    emit testChanged();
}

bool DeviceEditor::save()
{
    if (!m_context)
        return false;
    if (const QString problem = validate(); !problem.isEmpty()) {
        setError(problem);
        return false;
    }
    DeviceConfig device = current();
    if (device.name.isEmpty()) {
        QString fallback;
        for (const QVariant& f : fields::forSource(device.type)) {
            const QVariantMap m = f.toMap();
            if (m.value(QStringLiteral("discover")).toBool())
                fallback = value(m.value(QStringLiteral("key")).toString()).toString();
        }
        device.name = fallback.isEmpty() ? deviceinfo::typeName(device.type) : fallback;
    }
    AppConfig config = m_context->config();
    // Carry a change of account details over to the devices that shared them.
    if (!m_isNew && m_applyToShared && accountOf(device) != m_savedAccount) {
        for (DeviceConfig& other : config.devices) {
            if (other.id != device.id && other.type == device.type && accountOf(other) == m_savedAccount)
                copyAccount(device, other);
        }
    }
    const auto it = std::ranges::find(config.devices, device.id, &DeviceConfig::id);
    if (it == config.devices.end())
        config.devices.append(device);
    else
        *it = device;
    const bool ok = m_context->updateConfig(config);
    setError(ok ? QString() : m_context->lastError());
    m_device = device;
    m_isNew = false;
    m_dirty = false;
    m_reusedFrom.clear();
    updateSharing();
    emit loaded();
    emit edited();
    emit saved(idString(device.id));
    return ok;
}

bool DeviceEditor::remove()
{
    if (!m_context || m_isNew)
        return false;
    const QString id = deviceId();
    const bool ok = m_context->removeDevice(m_device.id);
    if (ok)
        emit removed(id);
    return ok;
}

void DeviceEditor::markEdited()
{
    m_dirty = true;
    emit edited();
}

void DeviceEditor::setError(const QString& error)
{
    if (error == m_error)
        return;
    m_error = error;
    emit errorChanged();
}

}  // namespace nutty
