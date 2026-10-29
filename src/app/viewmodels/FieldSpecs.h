// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Types.h"

#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// The form fields of each source and channel type, for the editors. Each
// field is a map:
//
//   key          settings/secrets key (Config.h keys::)
//   label, placeholder, help
//   kind         text, number, secret, bool, choice, multiline
//   options      [{ value, text }] for choice
//   min, max     for number
//   discover     true for the field the "Find devices" picker fills
//   advanced     shown under "Advanced"
//   account      belongs to the server or account, not the device: a new device
//                of a type starts with these from the last one (accountKeys)
//   showIf       { key: value } — only shown when another field has that value
//   required     must not be empty
//   defaultValue
namespace nutty::fields {

QVariantList forSource(SourceType type);
QVariantList forChannel(ChannelType type);
// The defaults of a new device/channel of that type.
QVariantMap sourceDefaults(SourceType type);
QVariantMap channelDefaults(ChannelType type);
// The keys of a source type's `account` fields.
QStringList accountKeys(SourceType type);

}  // namespace nutty::fields
