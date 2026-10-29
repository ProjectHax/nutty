// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/sources/ecoflow/EcoFlowMapping.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>

namespace nutty::ecoflow {

namespace {

class Text
{
    Q_DECLARE_TR_FUNCTIONS(nutty::EcoFlow)
};

// Watts below this count as "nothing flowing" (EcoFlow reports a few W of noise).
constexpr double kFlowThresholdW = 2.0;
constexpr double kAcPresentVolts = 50.0;

std::optional<double> numeric(const QJsonValue& v)
{
    if (v.isDouble())
        return v.toDouble();
    if (v.isBool())
        return v.toBool() ? 1.0 : 0.0;
    if (v.isString()) {
        bool ok = false;
        const double d = v.toString().trimmed().toDouble(&ok);
        if (ok)
            return d;
    }
    return std::nullopt;
}

QString rawText(const QJsonValue& v)
{
    if (v.isString())
        return v.toString();
    if (v.isDouble()) {
        const double d = v.toDouble();
        if (std::floor(d) == d && std::abs(d) < 1e15)
            return QString::number(static_cast<qint64>(d));
        return QString::number(d);
    }
    if (v.isBool())
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (v.isObject())
        return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    if (v.isArray())
        return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    return {};
}

}  // namespace

std::optional<qint64> validMinutes(std::optional<double> minutes)
{
    if (!minutes || *minutes <= 0 || *minutes > 5000)
        return std::nullopt;
    return static_cast<qint64>(*minutes + 0.5);
}

const EcoFlowMapping& EcoFlowMapping::builtin()
{
    static const EcoFlowMapping mapping = [] {
        QFile file(QString::fromLatin1(kResourcePath));
        if (!file.open(QIODevice::ReadOnly))
            return EcoFlowMapping {};
        return fromJson(file.readAll());
    }();
    return mapping;
}

EcoFlowMapping EcoFlowMapping::fromJson(const QByteArray& json)
{
    EcoFlowMapping mapping;
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    for (const QJsonValue& fv : root.value(u"families").toArray()) {
        const QJsonObject fo = fv.toObject();
        Family f;
        f.id = fo.value(u"id").toString();
        f.label = fo.value(u"label").toString();
        f.limited = fo.value(u"limited").toBool(false);
        f.battery = fo.value(u"battery").toBool(true);
        for (const QJsonValue& m : fo.value(u"match").toArray())
            f.match << m.toString();
        const QJsonObject fields = fo.value(u"fields").toObject();
        for (auto it = fields.begin(); it != fields.end(); ++it) {
            QList<Candidate> candidates;
            for (const QJsonValue& c : it.value().toArray()) {
                if (c.isString()) {
                    candidates.append({ c.toString(), 1.0 });
                } else {
                    const QJsonObject co = c.toObject();
                    candidates.append({ co.value(u"key").toString(), co.value(u"scale").toDouble(1.0) });
                }
            }
            f.fields.insert(it.key(), candidates);
        }
        parseDetails(f, fo.value(u"details").toObject());
        parseControls(f, fo.value(u"controls").toObject());
        if (!f.id.isEmpty())
            mapping.m_families << f;
    }
    return mapping;
}

void EcoFlowMapping::parseDetails(Family& family, const QJsonObject& details)
{
    const auto flows = [](const QJsonValue& list) {
        QList<Flow> out;
        for (const QJsonValue& v : list.toArray()) {
            const QJsonObject o = v.toObject();
            Flow flow { o.value(u"id").toString(), {} };
            for (const QJsonValue& k : o.value(u"keys").toArray())
                flow.keys << k.toString();
            if (!flow.id.isEmpty() && !flow.keys.isEmpty())
                out << flow;
        }
        return out;
    };
    const auto readings = [](const QJsonValue& list) {
        QList<Reading> out;
        for (const QJsonValue& v : list.toArray()) {
            const QJsonObject o = v.toObject();
            Reading r { o.value(u"id").toString(), o.value(u"key").toString() };
            if (!r.id.isEmpty() && !r.key.isEmpty())
                out << r;
        }
        return out;
    };
    family.inputs = flows(details.value(u"inputs"));
    family.outputs = flows(details.value(u"outputs"));
    family.energy = readings(details.value(u"energy"));
    family.temperatures = readings(details.value(u"temperatures"));
    for (const QJsonValue& v : details.value(u"packs").toArray()) {
        const QJsonObject o = v.toObject();
        Pack pack { o.value(u"id").toString(), o.value(u"prefix").toString(), o.value(u"online").toString() };
        if (!pack.id.isEmpty())
            family.packs << pack;
    }
}

void EcoFlowMapping::parseControls(Family& family, const QJsonObject& controls)
{
    for (const QJsonValue& v : controls.value(u"serials").toArray())
        family.controlSerials << v.toString();
    for (const QJsonValue& v : controls.value(u"items").toArray()) {
        const QJsonObject o = v.toObject();
        Control c;
        DeviceControl& d = c.control;
        d.id = o.value(u"id").toString();
        d.kind = enumFromKey(o.value(u"kind").toString(), ControlKind::Toggle);
        d.minimum = o.value(u"min").toDouble(0);
        d.maximum = o.value(u"max").toDouble(d.kind == ControlKind::Toggle ? 1 : 100);
        d.step = o.value(u"step").toDouble(1);
        for (const QJsonValue& choice : o.value(u"choices").toArray())
            d.choices << choice.toDouble();
        d.unit = o.value(u"unit").toString();
        d.dependsOn = o.value(u"dependsOn").toString();
        d.ask = enumFromKey(o.value(u"ask").toString(), ControlAsk::Once);
        c.state = o.value(u"state").toString();
        c.on = o.value(u"on").toDouble(1);
        c.off = o.value(u"off").toDouble(0);
        for (const QJsonValue& l : o.value(u"limits").toArray()) {
            const QJsonObject lo = l.toObject();
            c.limits.append(
                { lo.value(u"key").toString(), lo.value(u"below").toDouble(), lo.value(u"max").toDouble() });
        }
        c.params = o.value(u"params").toObject();
        if (!d.id.isEmpty() && !c.state.isEmpty() && !c.params.isEmpty())
            family.controls << c;
    }
}

QStringList EcoFlowMapping::familyIds() const
{
    QStringList ids;
    for (const auto& f : m_families)
        ids << f.id;
    return ids;
}

const EcoFlowMapping::Family* EcoFlowMapping::detect(const QJsonObject& quota) const
{
    for (const auto& f : m_families) {
        // An empty match list is the catch-all.
        if (f.match.isEmpty())
            return &f;
        if (std::ranges::any_of(f.match, [&](const QString& key) { return quota.contains(key); }))
            return &f;
    }
    return nullptr;
}

std::optional<double> EcoFlowMapping::field(
    const Family& family, const QJsonObject& quota, const QString& name)
{
    const auto it = family.fields.constFind(name);
    if (it == family.fields.cend())
        return std::nullopt;
    for (const Candidate& c : *it) {
        if (!quota.contains(c.key))
            continue;
        if (const auto v = numeric(quota.value(c.key)))
            return *v * c.scale;
    }
    return std::nullopt;
}

EcoFlowMapping::Result EcoFlowMapping::map(const QJsonObject& quota) const
{
    Result r;
    DeviceStatus& s = r.status;
    for (auto it = quota.begin(); it != quota.end(); ++it)
        s.raw.insert(it.key(), rawText(it.value()));
    s.manufacturer = QStringLiteral("EcoFlow");

    const Family* family = detect(quota);
    if (!family) {
        s.flags = StatusFlag::Online;
        return r;
    }
    r.familyId = family->id;
    r.familyLabel = family->label;
    r.limited = family->limited;
    r.hasBattery = family->battery;

    const auto get = [&](const char* name) { return field(*family, quota, QString::fromLatin1(name)); };

    if (const auto soc = get("charge"))
        s.charge = std::clamp(*soc, 0.0, 100.0);
    s.wattsIn = get("wattsIn");
    s.wattsOut = get("wattsOut");
    s.wattsSolar = get("solarWatts");
    s.inputVoltage = get("acInVoltage");
    s.outputVoltage = get("acOutVoltage");
    s.batteryVoltage = get("batteryVoltage");
    s.temperature = get("temperature");

    // Is mains (AC) present? Any of voltage, frequency or AC input power can
    // tell; when none is reported, fall back to "is anything flowing in".
    std::optional<bool> acPresent;
    if (const auto v = get("acInVoltage"))
        acPresent = *v > kAcPresentVolts;
    if (const auto f = get("acInFrequency"))
        acPresent = acPresent.value_or(false) || *f > 0;
    if (const auto w = get("acInWatts"))
        acPresent = acPresent.value_or(false) || *w > kFlowThresholdW;

    const double in = s.wattsIn.value_or(0);
    const double out = s.wattsOut.value_or(0);

    StatusFlags flags;
    bool onBattery = false;
    if (!r.hasBattery) {
        flags = StatusFlag::Online;
    } else {
        if (acPresent)
            onBattery = !*acPresent;
        else
            onBattery = in <= kFlowThresholdW && out > kFlowThresholdW;
        flags = onBattery ? StatusFlag::OnBattery : StatusFlag::Online;
        if (onBattery && out > kFlowThresholdW)
            flags |= StatusFlag::Discharging;
        if (in > kFlowThresholdW && s.charge && *s.charge < 99.5)
            flags |= StatusFlag::Charging;

        // EcoFlow has no low-battery flag; derive one from the discharge
        // limit the user set on the device (or 10 %).
        const auto minSoc = get("minDischargeSoc");
        const double low = (minSoc && *minSoc > 0) ? *minSoc + 5.0 : 10.0;
        s.chargeLow = low;
        if (onBattery && s.charge && *s.charge <= low)
            flags |= StatusFlag::LowBattery;
    }
    s.flags = flags;

    // Runtime: the discharge estimate when present, else (only while
    // discharging, as it may be a charge time otherwise) the generic remaining
    // time, whose sign varies by model.
    std::optional<qint64> minutes = validMinutes(get("dischargeMinutes"));
    if (!minutes && onBattery) {
        if (const auto remain = get("remainMinutes"))
            minutes = validMinutes(std::abs(*remain));
    }
    if (minutes)
        s.runtime = *minutes * 60;
    return r;
}

DeviceDetails EcoFlowMapping::details(const QJsonObject& quota) const
{
    DeviceDetails d;
    const Family* family = detect(quota);
    if (!family)
        return d;
    const auto value = [&](const QString& key) { return numeric(quota.value(key)); };

    const auto flows = [&](const QList<Flow>& specs, QList<DeviceDetails::Reading>& out) {
        for (const Flow& flow : specs) {
            std::optional<double> total;
            for (const QString& key : flow.keys) {
                if (const auto v = value(key))
                    total = total.value_or(0) + *v;
            }
            if (total)
                out.append({ flow.id, std::max(0.0, *total) });
        }
    };
    flows(family->inputs, d.inputs);
    flows(family->outputs, d.outputs);

    for (const Pack& spec : family->packs) {
        if (!spec.online.isEmpty() && value(spec.online).value_or(1) == 0)
            continue;
        const auto get = [&](const char* name, double scale = 1.0) -> std::optional<double> {
            if (const auto v = value(spec.prefix + QLatin1StringView(name)))
                return *v * scale;
            return std::nullopt;
        };
        DeviceDetails::Pack pack;
        pack.id = spec.id;
        pack.charge = get("soc");
        if (!pack.charge)
            continue;
        pack.health = get("soh");
        if (const auto cycles = get("cycles"))
            pack.cycles = static_cast<int>(*cycles);
        pack.temperature = get("temp");
        pack.voltage = get("vol", 0.001);
        pack.current = get("amp", 0.001);
        pack.fullCapacity = get("fullCap", 0.001);
        pack.designCapacity = get("designCap", 0.001);
        pack.remainingCapacity = get("remainCap", 0.001);
        pack.cellMin = get("minCellVol", 0.001);
        pack.cellMax = get("maxCellVol", 0.001);
        d.packs << pack;
    }

    for (const Reading& r : family->energy) {
        // 2^32 - 1 is a counter the device doesn't keep.
        if (const auto v = value(r.key); v && *v > 0 && *v < 4294967295.0)
            d.energy.append({ r.id, *v });
    }
    for (const Reading& r : family->temperatures) {
        if (const auto v = value(r.key))
            d.temperatures.append({ r.id, *v });
    }
    d.chargeMinutes = validMinutes(field(*family, quota, QStringLiteral("chargeMinutes")));
    return d;
}

const EcoFlowMapping::Family* EcoFlowMapping::controlFamily(
    const QJsonObject& quota, const QString& serial) const
{
    const Family* family = detect(quota);
    if (!family || family->controls.isEmpty())
        return nullptr;
    // Products in one family can take different commands, so the table
    // names the serial numbers it was written for.
    if (!family->controlSerials.isEmpty()
        && std::ranges::none_of(family->controlSerials,
            [&](const QString& prefix) { return serial.startsWith(prefix, Qt::CaseInsensitive); }))
        return nullptr;
    return family;
}

DeviceControl EcoFlowMapping::current(const Control& c, const QJsonObject& quota)
{
    DeviceControl d = c.control;
    for (const Limit& limit : c.limits) {
        if (const auto v = numeric(quota.value(limit.key)); v && *v < limit.below)
            d.maximum = std::min(d.maximum, limit.maximum);
    }
    if (const auto v = numeric(quota.value(c.state))) {
        if (d.kind != ControlKind::Toggle)
            d.value = *v;
        else if (*v == c.on)
            d.value = 1;
        else if (*v == c.off)
            d.value = 0;
        // Anything else leaves the toggle unknown rather than guessing.
    }
    return d;
}

QList<DeviceControl> EcoFlowMapping::controls(const QJsonObject& quota, const QString& serial) const
{
    QList<DeviceControl> out;
    if (const Family* family = controlFamily(quota, serial)) {
        for (const Control& c : family->controls)
            out << current(c, quota);
    }
    return out;
}

std::expected<QJsonObject, QString> EcoFlowMapping::command(
    const QJsonObject& quota, const QString& serial, const QString& id, double value) const
{
    const Family* family = controlFamily(quota, serial);
    if (!family)
        return std::unexpected(Text::tr("nutty can't change the settings of this EcoFlow model."));
    const auto it = std::ranges::find(family->controls, id, [](const Control& c) { return c.control.id; });
    if (it == family->controls.end())
        return std::unexpected(Text::tr("This EcoFlow model doesn't have that setting."));
    const Control& c = *it;
    const DeviceControl now = current(c, quota);
    if (!now.accepts(value))
        return std::unexpected(Text::tr("The device doesn't take that value."));
    if (!now.dependsOn.isEmpty()) {
        const auto parent = std::ranges::find(
            family->controls, now.dependsOn, [](const Control& p) { return p.control.id; });
        if (parent == family->controls.end() || current(*parent, quota).value.value_or(0) != 1)
            return std::unexpected(Text::tr("Turn on the setting this one belongs to first."));
    }

    const double sent = c.control.kind == ControlKind::Toggle ? (value != 0 ? c.on : c.off) : value;
    QJsonObject params;
    for (auto p = c.params.begin(); p != c.params.end(); ++p) {
        const QString text = p.value().toString();
        if (!p.value().isString() || !text.startsWith(u'$')) {
            params.insert(p.key(), p.value());
            continue;
        }
        const QString name = text.mid(1);
        if (name == u"value") {
            params.insert(p.key(), sent);
            continue;
        }
        // Other settings the same command carries are sent as they are now.
        const auto reading = numeric(quota.value(name));
        if (!reading)
            return std::unexpected(
                Text::tr("EcoFlow didn't report %1, which this change needs. Try again in a moment.")
                    .arg(name));
        params.insert(p.key(), *reading);
    }
    return params;
}

}  // namespace nutty::ecoflow
