// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "app/viewmodels/Labels.h"

#include "app/viewmodels/DeviceInfo.h"
#include "core/engine/EventText.h"

#include <QCoreApplication>
#include <QMetaEnum>

namespace nutty::labels {

namespace {
template <typename E> QList<E> allValues()
{
    QList<E> list;
    const QMetaEnum meta = QMetaEnum::fromType<E>();
    for (int i = 0; i < meta.keyCount(); ++i)
        list << static_cast<E>(meta.value(i));
    return list;
}
}  // namespace

QString eventTypeName(EventType type)
{
    switch (type) {
    case EventType::OnBattery:
        return QCoreApplication::translate("nutty::Labels", "On battery");
    case EventType::PowerRestored:
        return QCoreApplication::translate("nutty::Labels", "Power restored");
    case EventType::LowBattery:
        return QCoreApplication::translate("nutty::Labels", "Low battery");
    case EventType::ChargeBelow:
        return QCoreApplication::translate("nutty::Labels", "Charge below threshold");
    case EventType::RuntimeBelow:
        return QCoreApplication::translate("nutty::Labels", "Runtime below threshold");
    case EventType::ForcedShutdown:
        return QCoreApplication::translate("nutty::Labels", "Forced shutdown");
    case EventType::ReplaceBattery:
        return QCoreApplication::translate("nutty::Labels", "Replace battery");
    case EventType::Overload:
        return QCoreApplication::translate("nutty::Labels", "Overload");
    case EventType::Bypass:
        return QCoreApplication::translate("nutty::Labels", "Bypass");
    case EventType::UpsOff:
        return QCoreApplication::translate("nutty::Labels", "UPS off");
    case EventType::Alarm:
        return QCoreApplication::translate("nutty::Labels", "Alarm");
    case EventType::CommLost:
        return QCoreApplication::translate("nutty::Labels", "Connection lost");
    case EventType::CommRestored:
        return QCoreApplication::translate("nutty::Labels", "Connection restored");
    case EventType::AuthFailed:
        return QCoreApplication::translate("nutty::Labels", "Sign-in failed");
    case EventType::ActionScheduled:
        return QCoreApplication::translate("nutty::Labels", "Action scheduled");
    case EventType::ActionCancelled:
        return QCoreApplication::translate("nutty::Labels", "Action cancelled");
    case EventType::ActionExecuted:
        return QCoreApplication::translate("nutty::Labels", "Action carried out");
    case EventType::ActionFailed:
        return QCoreApplication::translate("nutty::Labels", "Action failed");
    case EventType::Test:
        return QCoreApplication::translate("nutty::Labels", "Test notification");
    case EventType::SettingChanged:
        return QCoreApplication::translate("nutty::Labels", "Device setting changed");
    case EventType::SettingFailed:
        return QCoreApplication::translate("nutty::Labels", "Device setting not changed");
    }
    return {};
}

QString channelTypeName(ChannelType type)
{
    switch (type) {
    case ChannelType::Desktop:
        return QCoreApplication::translate("nutty::Labels", "Desktop");
    case ChannelType::Pushover:
        return QCoreApplication::translate("nutty::Labels", "Pushover");
    case ChannelType::Ntfy:
        return QCoreApplication::translate("nutty::Labels", "ntfy");
    case ChannelType::Email:
        return QCoreApplication::translate("nutty::Labels", "Email");
    case ChannelType::Webhook:
        return QCoreApplication::translate("nutty::Labels", "Webhook");
    case ChannelType::PagerDuty:
        return QCoreApplication::translate("nutty::Labels", "PagerDuty");
    }
    return {};
}

QString channelTypeDescription(ChannelType type)
{
    switch (type) {
    case ChannelType::Desktop:
        return QCoreApplication::translate("nutty::Labels", "Notifications on this computer");
    case ChannelType::Pushover:
        return QCoreApplication::translate("nutty::Labels", "Push to your phone with Pushover");
    case ChannelType::Ntfy:
        return QCoreApplication::translate("nutty::Labels", "Publish to an ntfy topic");
    case ChannelType::Email:
        return QCoreApplication::translate("nutty::Labels", "Send mail through an SMTP server");
    case ChannelType::Webhook:
        return QCoreApplication::translate("nutty::Labels", "POST to Discord, Slack, Teams or any URL");
    case ChannelType::PagerDuty:
        return QCoreApplication::translate("nutty::Labels", "Open and resolve PagerDuty alerts");
    }
    return {};
}

QString channelIcon(ChannelType type)
{
    switch (type) {
    case ChannelType::Desktop:
        return QStringLiteral("monitor");
    case ChannelType::Pushover:
        return QStringLiteral("phone");
    case ChannelType::Ntfy:
        return QStringLiteral("message");
    case ChannelType::Email:
        return QStringLiteral("mail");
    case ChannelType::Webhook:
        return QStringLiteral("link");
    case ChannelType::PagerDuty:
        return QStringLiteral("bell");
    }
    return QStringLiteral("bell");
}

QString sourceTypeDescription(SourceType type)
{
    switch (type) {
    case SourceType::Nut:
        return QCoreApplication::translate("nutty::Labels", "A UPS on a Network UPS Tools server");
    case SourceType::Apcupsd:
        return QCoreApplication::translate("nutty::Labels", "An APC UPS through apcupsd");
    case SourceType::EcoFlow:
        return QCoreApplication::translate("nutty::Labels", "An EcoFlow power station (Open API)");
    case SourceType::HttpJson:
        return QCoreApplication::translate("nutty::Labels", "Any JSON endpoint, or Home Assistant");
    case SourceType::System:
        return QCoreApplication::translate("nutty::Labels", "This computer's battery or USB UPS");
    }
    return {};
}

QString sourceIcon(SourceType type)
{
    switch (type) {
    case SourceType::Nut:
    case SourceType::Apcupsd:
        return QStringLiteral("server");
    case SourceType::EcoFlow:
        return QStringLiteral("battery-charging");
    case SourceType::HttpJson:
        return QStringLiteral("globe");
    case SourceType::System:
        return QStringLiteral("laptop");
    }
    return QStringLiteral("battery");
}

QString conditionName(ConditionType condition)
{
    switch (condition) {
    case ConditionType::OnBatteryFor:
        return QCoreApplication::translate("nutty::Labels", "On battery for");
    case ConditionType::ChargeBelow:
        return QCoreApplication::translate("nutty::Labels", "Charge below");
    case ConditionType::RuntimeBelow:
        return QCoreApplication::translate("nutty::Labels", "Runtime below");
    case ConditionType::LowBattery:
        return QCoreApplication::translate("nutty::Labels", "Low battery signal");
    case ConditionType::ForcedShutdown:
        return QCoreApplication::translate("nutty::Labels", "Forced shutdown (FSD)");
    case ConditionType::CommLostOnBattery:
        return QCoreApplication::translate("nutty::Labels", "Connection lost while on battery");
    }
    return {};
}

QString conditionUnit(ConditionType condition)
{
    switch (condition) {
    case ConditionType::ChargeBelow:
        return QStringLiteral("%");
    case ConditionType::OnBatteryFor:
    case ConditionType::RuntimeBelow:
        return QStringLiteral("s");
    default:
        return {};
    }
}

QString conditionText(ConditionType condition, double threshold)
{
    switch (condition) {
    case ConditionType::OnBatteryFor:
        return QCoreApplication::translate("nutty::Labels", "On battery for %1")
            .arg(text::duration(static_cast<qint64>(threshold)));
    case ConditionType::ChargeBelow:
        return QCoreApplication::translate("nutty::Labels", "Charge below %1").arg(text::percent(threshold));
    case ConditionType::RuntimeBelow:
        return QCoreApplication::translate("nutty::Labels", "Runtime below %1")
            .arg(text::duration(static_cast<qint64>(threshold)));
    default:
        return conditionName(condition);
    }
}

QString scopeName(RuleScope scope)
{
    switch (scope) {
    case RuleScope::Device:
        return QCoreApplication::translate("nutty::Labels", "A device");
    case RuleScope::GroupAny:
        return QCoreApplication::translate("nutty::Labels", "Any device in a group");
    case RuleScope::GroupAll:
        return QCoreApplication::translate("nutty::Labels", "Every device in a group");
    }
    return {};
}

QString kindName(DeviceKind kind)
{
    switch (kind) {
    case DeviceKind::Auto:
        return QCoreApplication::translate("nutty::Labels", "Automatic");
    case DeviceKind::TowerUps:
        return QCoreApplication::translate("nutty::Labels", "Tower UPS");
    case DeviceKind::RackUps:
        return QCoreApplication::translate("nutty::Labels", "Rack UPS");
    case DeviceKind::PowerStation:
        return QCoreApplication::translate("nutty::Labels", "Power station");
    case DeviceKind::HomeBattery:
        return QCoreApplication::translate("nutty::Labels", "Home battery");
    case DeviceKind::Laptop:
        return QCoreApplication::translate("nutty::Labels", "Laptop");
    case DeviceKind::Generic:
        return QCoreApplication::translate("nutty::Labels", "Battery");
    }
    return {};
}

QVariantList sourceTypes()
{
    QVariantList list;
    for (const SourceType t : allValues<SourceType>()) {
        list.append(QVariantMap {
            { QStringLiteral("key"), enumKey(t) },
            { QStringLiteral("name"), deviceinfo::typeName(t) },
            { QStringLiteral("description"), sourceTypeDescription(t) },
            { QStringLiteral("icon"), sourceIcon(t) },
        });
    }
    return list;
}

QVariantList channelTypes()
{
    QVariantList list;
    for (const ChannelType t : allValues<ChannelType>()) {
        list.append(QVariantMap {
            { QStringLiteral("key"), enumKey(t) },
            { QStringLiteral("name"), channelTypeName(t) },
            { QStringLiteral("description"), channelTypeDescription(t) },
            { QStringLiteral("icon"), channelIcon(t) },
        });
    }
    return list;
}

QVariantList eventTypes()
{
    QVariantList list;
    for (const EventType t : allValues<EventType>()) {
        if (t == EventType::Test)
            continue;
        QString group;
        switch (t) {
        case EventType::OnBattery:
        case EventType::PowerRestored:
        case EventType::ForcedShutdown:
        case EventType::Bypass:
        case EventType::Overload:
        case EventType::UpsOff:
            group = QCoreApplication::translate("nutty::Labels", "Power");
            break;
        case EventType::LowBattery:
        case EventType::ChargeBelow:
        case EventType::RuntimeBelow:
        case EventType::ReplaceBattery:
        case EventType::Alarm:
            group = QCoreApplication::translate("nutty::Labels", "Battery");
            break;
        case EventType::CommLost:
        case EventType::CommRestored:
        case EventType::AuthFailed:
            group = QCoreApplication::translate("nutty::Labels", "Connection");
            break;
        case EventType::SettingChanged:
        case EventType::SettingFailed:
            group = QCoreApplication::translate("nutty::Labels", "Device settings");
            break;
        default:
            group = QCoreApplication::translate("nutty::Labels", "Power actions");
            break;
        }
        list.append(QVariantMap {
            { QStringLiteral("key"), enumKey(t) },
            { QStringLiteral("name"), eventTypeName(t) },
            { QStringLiteral("group"), group },
        });
    }
    return list;
}

QVariantList conditions()
{
    QVariantList list;
    for (const ConditionType c : allValues<ConditionType>()) {
        list.append(QVariantMap {
            { QStringLiteral("key"), enumKey(c) },
            { QStringLiteral("name"), conditionName(c) },
            { QStringLiteral("unit"), conditionUnit(c) },
        });
    }
    return list;
}

QVariantList actionTypes()
{
    QVariantList list;
    for (const ActionType a : allValues<ActionType>()) {
        list.append(QVariantMap {
            { QStringLiteral("key"), enumKey(a) },
            { QStringLiteral("name"), text::actionName(a) },
        });
    }
    return list;
}

QVariantList scopes()
{
    QVariantList list;
    for (const RuleScope s : allValues<RuleScope>())
        list.append(
            QVariantMap { { QStringLiteral("key"), enumKey(s) }, { QStringLiteral("name"), scopeName(s) } });
    return list;
}

QVariantList deviceKinds()
{
    QVariantList list;
    for (const DeviceKind k : allValues<DeviceKind>())
        list.append(
            QVariantMap { { QStringLiteral("key"), enumKey(k) }, { QStringLiteral("name"), kindName(k) } });
    return list;
}

// --- device settings --------------------------------------------------------

namespace {

struct ControlText {
    const char* id;
    const char* group;
    const char* icon;
    const char* description;
};

constexpr ControlText kControls[] = {
    { "acOutput", "outputs", "outlet", QT_TRANSLATE_NOOP("nutty::Labels", "The AC outlets.") },
    { "xBoost", "outputs", "boost",
        QT_TRANSLATE_NOOP("nutty::Labels",
            "Runs heavy appliances, like kettles and heaters, at a lower voltage instead of shutting off.") },
    { "acTimeout", "outputs", "timer",
        QT_TRANSLATE_NOOP(
            "nutty::Labels", "Turns the AC outlets off when nothing has drawn power for this long.") },
    { "carOutput", "outputs", "car", QT_TRANSLATE_NOOP("nutty::Labels", "The 12 V car outlet.") },
    { "acChargeSpeed", "charging", "charge-speed",
        QT_TRANSLATE_NOOP("nutty::Labels",
            "How fast it charges from wall power. The charge speed switch on the back can still hold it to "
            "400 W, whatever this says.") },
    { "carInputCurrent", "charging", "car-charge",
        QT_TRANSLATE_NOOP("nutty::Labels", "How much current it draws when charging from a car.") },
    { "chargeLimit", "battery", "charge-limit",
        QT_TRANSLATE_NOOP(
            "nutty::Labels", "Stops charging at this level. A lower limit is easier on the battery.") },
    { "dischargeLimit", "battery", "discharge-limit",
        QT_TRANSLATE_NOOP("nutty::Labels",
            "Stops powering anything below this level, keeping some charge in the battery.") },
    { "backupReserve", "battery", "reserve",
        QT_TRANSLATE_NOOP("nutty::Labels", "Holds some charge back for when the power goes out.") },
    { "backupReserveLevel", "battery", "reserve-level",
        QT_TRANSLATE_NOOP("nutty::Labels", "How much charge the backup reserve holds.") },
    { "beeper", "unit", "volume",
        QT_TRANSLATE_NOOP("nutty::Labels", "Beeps for alerts and button presses.") },
    { "screenTimeout", "unit", "screen-off",
        QT_TRANSLATE_NOOP("nutty::Labels", "Turns the screen off after this long.") },
    { "unitTimeout", "unit", "standby",
        QT_TRANSLATE_NOOP("nutty::Labels", "Turns the whole unit off after it has sat idle this long.") },
    { "generatorStart", "generator", "generator",
        QT_TRANSLATE_NOOP(
            "nutty::Labels", "Starts a connected smart generator when the charge falls to this level.") },
    { "generatorStop", "generator", "generator-off",
        QT_TRANSLATE_NOOP("nutty::Labels", "Stops the generator once the charge reaches this level.") },
};

const ControlText* controlText(const QString& id)
{
    for (const ControlText& c : kControls) {
        if (id == QLatin1StringView(c.id))
            return &c;
    }
    return nullptr;
}

}  // namespace

QString controlDescription(const QString& id)
{
    const ControlText* c = controlText(id);
    return c ? QCoreApplication::translate("nutty::Labels", c->description) : QString();
}

QString controlIcon(const QString& id)
{
    const ControlText* c = controlText(id);
    return c ? QString::fromLatin1(c->icon) : QStringLiteral("sliders");
}

QString controlGroup(const QString& id)
{
    const ControlText* c = controlText(id);
    return c ? QString::fromLatin1(c->group) : QStringLiteral("unit");
}

QString controlGroupName(const QString& group)
{
    if (group == u"outputs")
        return QCoreApplication::translate("nutty::Labels", "Outputs");
    if (group == u"charging")
        return QCoreApplication::translate("nutty::Labels", "Charging");
    if (group == u"battery")
        return QCoreApplication::translate("nutty::Labels", "Battery");
    if (group == u"generator")
        return QCoreApplication::translate("nutty::Labels", "Generator");
    return QCoreApplication::translate("nutty::Labels", "Unit");
}

QString controlWarning(const DeviceControl& control, double value, const QString& device)
{
    if (!control.asksTwice(value))
        return {};
    const QString& id = control.id;
    const QString to = text::controlValue(control, value);
    if (id == u"acOutput")
        return value != 0 ? QCoreApplication::translate("nutty::Labels",
                                "%1's AC outlets come on and power whatever is plugged into them.")
                                .arg(device)
                          : QCoreApplication::translate("nutty::Labels",
                                "Everything plugged into %1's AC outlets loses power right away.")
                                .arg(device);
    if (id == u"xBoost")
        return value != 0 ? QCoreApplication::translate("nutty::Labels",
                                "With X-Boost on, the AC outlets lower their voltage to run heavy loads "
                                "instead of shutting off. Some electronics don't work right at the lower "
                                "voltage.")
                          : QCoreApplication::translate("nutty::Labels",
                                "With X-Boost off, the AC outlets shut off when something draws more than "
                                "they can supply, and everything plugged in loses power.");
    if (id == u"acChargeSpeed") {
        if (control.value && value <= *control.value)
            return QCoreApplication::translate("nutty::Labels",
                "%2 will take longer to charge at %1. The charge speed switch on the back of %2 may still "
                "hold it to 400 W, so the change might not show.")
                .arg(to, device);
        return QCoreApplication::translate("nutty::Labels",
            "Charging at %1 draws more from the wall outlet or generator, and can trip its breaker if other "
            "things share the circuit. The charge speed switch on the back of %2 may still hold it to 400 W, "
            "so the change might not show.")
            .arg(to, device);
    }
    if (id == u"carOutput")
        return QCoreApplication::translate(
            "nutty::Labels", "Anything powered from %1's 12 V car outlet loses power right away.")
            .arg(device);
    if (id == u"acTimeout")
        return QCoreApplication::translate("nutty::Labels",
            "If what's plugged into %1's AC outlets draws very little for %2, the outlets turn off and it "
            "loses power.")
            .arg(device, to);
    if (id == u"unitTimeout")
        return QCoreApplication::translate("nutty::Labels",
            "If %1 sits idle for %2, it turns itself off, and everything plugged into it loses power.")
            .arg(device, to);
    return QCoreApplication::translate(
        "nutty::Labels", "This change can cut power to what's plugged into %1.")
        .arg(device);
}

QString detailName(const QString& section, const QString& id)
{
    if (section == u"packs") {
        if (id == u"main")
            return QCoreApplication::translate("nutty::Labels", "Main battery");
        if (id.startsWith(u"extra"))
            return QCoreApplication::translate("nutty::Labels", "Extra battery %1").arg(id.mid(5));
        return id;
    }
    if (section == u"energy") {
        if (id == u"acIn")
            return QCoreApplication::translate("nutty::Labels", "AC in");
        if (id == u"solarIn")
            return QCoreApplication::translate("nutty::Labels", "Solar in");
        if (id == u"dcIn")
            return QCoreApplication::translate("nutty::Labels", "Car in");
        if (id == u"acOut")
            return QCoreApplication::translate("nutty::Labels", "AC out");
        if (id == u"dcOut")
            return QCoreApplication::translate("nutty::Labels", "DC out");
        return id;
    }
    if (section == u"temperatures") {
        if (id == u"inverter")
            return QCoreApplication::translate("nutty::Labels", "Inverter");
        if (id == u"solar")
            return QCoreApplication::translate("nutty::Labels", "Solar charger");
        if (id == u"dcInput")
            return QCoreApplication::translate("nutty::Labels", "DC input");
        return id;
    }
    // inputs / outputs
    if (id == u"ac")
        return QCoreApplication::translate("nutty::Labels", "AC");
    if (id == u"solar")
        return QCoreApplication::translate("nutty::Labels", "Solar / car");
    if (id == u"usb")
        return QCoreApplication::translate("nutty::Labels", "USB-A");
    if (id == u"usbc")
        return QCoreApplication::translate("nutty::Labels", "USB-C");
    if (id == u"car")
        return QCoreApplication::translate("nutty::Labels", "12 V");
    if (id == u"wireless")
        return QCoreApplication::translate("nutty::Labels", "Wireless");
    return id;
}

QString detailIcon(const QString& section, const QString& id)
{
    if (section == u"packs")
        return QStringLiteral("battery");
    if (section == u"temperatures")
        return QStringLiteral("thermometer");
    if (id == u"ac" || id == u"acIn")
        return section == u"outputs" ? QStringLiteral("outlet") : QStringLiteral("plug");
    if (id == u"acOut")
        return QStringLiteral("outlet");
    if (id == u"solar" || id == u"solarIn")
        return QStringLiteral("solar");
    if (id == u"dcIn")
        return QStringLiteral("car-charge");
    if (id == u"usb" || id == u"dcOut")
        return QStringLiteral("usb");
    if (id == u"usbc")
        return QStringLiteral("usb-c");
    if (id == u"car")
        return QStringLiteral("car");
    if (id == u"wireless")
        return QStringLiteral("wireless");
    return QStringLiteral("bolt");
}

}  // namespace nutty::labels
