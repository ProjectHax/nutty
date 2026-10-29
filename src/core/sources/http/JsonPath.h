// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include <QJsonValue>
#include <QStringView>

#include <optional>

namespace nutty::json {

// Resolves a path into a JSON document:
//   battery.soc        object members
//   data[0].value      array index
//   ["pd.soc"]         a member whose name contains dots (or 'single' quotes)
// Returns nullopt when any step is missing.
std::optional<QJsonValue> resolve(const QJsonValue& root, QStringView path);

// A JSON value as a number: numbers, numeric strings and booleans (1/0).
std::optional<double> toNumber(const QJsonValue& value);

// A JSON value as text, for status matching (numbers without trailing .0).
QString toText(const QJsonValue& value);

}  // namespace nutty::json
