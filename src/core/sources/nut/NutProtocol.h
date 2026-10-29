// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/DeviceStatus.h"
#include "core/model/Types.h"

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QStringView>

// Pure helpers for the NUT network protocol (upsd, TCP 3493): tokenizing reply
// lines, quoting command arguments, classifying replies and mapping the
// variables of `LIST VAR` onto a DeviceStatus. No I/O here.
namespace nutty::nut {

inline constexpr quint16 kDefaultPort = 3493;

// Splits one line into words. Double-quoted words may contain spaces and the
// escapes \" and \\; the quotes themselves are removed.
QStringList tokenize(QStringView line);

// Quotes an argument when it is empty or contains spaces, quotes or
// backslashes; plain words are returned unchanged.
QString quote(QStringView arg);

// Builds a command line ("LIST VAR ups\n"), quoting arguments as needed.
QByteArray command(const QStringList& words);

enum class LineKind {
    Ok,  // "OK ..."
    Err,  // "ERR <code> [...]"
    Begin,  // "BEGIN LIST ..."
    End,  // "END LIST ..."
    Var,  // "VAR <ups> <name> <value>"
    Ups,  // "UPS <name> <description>"
    Other,  // anything else (e.g. the VER/NETVER text reply)
};

struct Line {
    LineKind kind = LineKind::Other;
    QStringList words;  // the tokenized line
    QString errorCode;  // for Err, e.g. "ACCESS-DENIED"
};

Line parseLine(QStringView line);

// Maps an ERR code onto the shared error kinds. `duringStartTls` turns the
// FEATURE-* family into a TLS failure (the server can't do STARTTLS).
SourceError errorKind(QStringView code, bool duringStartTls = false);

// A short, translated explanation of an ERR code for the UI.
QString errorDescription(QStringView code);

// Normalizes the variables of one UPS (LIST VAR) into a DeviceStatus. `raw`
// receives every variable verbatim.
DeviceStatus statusFromVariables(const QMap<QString, QString>& vars);

}  // namespace nutty::nut
