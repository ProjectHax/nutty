// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Config.h"
#include "core/model/DeviceControl.h"

#include <QString>
#include <QVariantList>

// Translated names for the config's enums and a one-line description of a
// rule, shared by the view-models.
namespace nutty::labels {

QString eventTypeName(EventType type);
QString channelTypeName(ChannelType type);
QString channelTypeDescription(ChannelType type);
QString channelIcon(ChannelType type);
QString sourceTypeDescription(SourceType type);
QString sourceIcon(SourceType type);
QString conditionName(ConditionType condition);
// The threshold's unit: "%", "s", or empty when it has none.
QString conditionUnit(ConditionType condition);
// "Battery below 20 %", "On battery for 5 min", "Forced shutdown".
QString conditionText(ConditionType condition, double threshold);
QString scopeName(RuleScope scope);
QString kindName(DeviceKind kind);

// [{ key, name, description, icon }] for every source / channel type.
QVariantList sourceTypes();
QVariantList channelTypes();
// [{ key, name, group }] for every event type a channel can subscribe to.
QVariantList eventTypes();
// [{ key, name, unit }] / [{ key, name }] for the rule editor's pickers.
QVariantList conditions();
QVariantList actionTypes();
QVariantList scopes();
QVariantList deviceKinds();

// Settings on a device (DeviceControl ids), for the device page.
QString controlDescription(const QString& id);
QString controlIcon(const QString& id);
// outputs, charging, battery, unit, generator.
QString controlGroup(const QString& id);
QString controlGroupName(const QString& group);
// The second question before a change that can cut power, for the device
// named `device`. Empty when the change gets only one question.
QString controlWarning(const DeviceControl& control, double value, const QString& device);
// Names and icons for DeviceDetails items. section: inputs, outputs, packs,
// energy, temperatures.
QString detailName(const QString& section, const QString& id);
QString detailIcon(const QString& section, const QString& id);

}  // namespace nutty::labels
