// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Event.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QPair>
#include <QString>

// Helpers shared by the notification channels: header parsing, template
// rendering, encodings.
namespace nutty::notify {

// "info" | "warning" | "critical"
QString severityKey(Severity severity);

// Parses "Name: value" lines (newline separated); blank and malformed lines
// are skipped.
QList<QPair<QByteArray, QByteArray>> parseHeaderLines(const QString& text);

// Escapes a string for use inside a JSON string literal (no surrounding quotes).
QString jsonEscape(const QString& text);

// The {placeholder} values for an event: event, severity, device, device_id,
// title, message, charge, runtime, runtime_min, load, status, timestamp,
// timestamp_local. Missing measurements are empty strings.
QHash<QString, QString> eventPlaceholders(const Event& event);

// Replaces {name} tokens that exist in values; unknown tokens stay as written.
QString renderTemplate(const QString& tmpl, const QHash<QString, QString>& values, bool jsonEscapeValues);

// RFC 2047 encoded-word(s) for a header value; plain ASCII is returned as is.
// Long values are split into several encoded words joined by folding whitespace.
QByteArray encodeHeaderWord(const QString& text);

// application/x-www-form-urlencoded body (spaces as %20, '+' escaped).
QByteArray formEncode(const QList<QPair<QString, QString>>& fields);

// Multi-line plain-text description of an event: message, then device,
// status, charge, runtime and time. Used for email bodies.
QString describeEvent(const Event& event);

}  // namespace nutty::notify
