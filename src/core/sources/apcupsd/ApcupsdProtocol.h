// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/DeviceStatus.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QMap>
#include <QString>

#include <optional>

// Pure helpers for apcupsd's Network Information Server (NIS, TCP 3551). Every
// message is a record: a 2-byte big-endian length followed by that many bytes.
// The client sends "status"; the server answers with one record per
// "KEY : value" line and ends with a zero-length record.
namespace nutty::apcupsd {

inline constexpr quint16 kDefaultPort = 3551;

QByteArray encodeRecord(QByteArrayView payload);

// Accumulates bytes and splits them into records.
class RecordReader
{
public:
    void append(QByteArrayView bytes);
    // True once the zero-length terminator has been read.
    bool isComplete() const { return m_complete; }
    const QList<QByteArray>& records() const { return m_records; }
    void clear();

private:
    QByteArray m_buffer;
    QList<QByteArray> m_records;
    bool m_complete = false;
};

// "STATUS   : ONLINE \n" -> {"STATUS", "ONLINE"}.
QMap<QString, QString> parseStatusRecords(const QList<QByteArray>& records);

// The leading number of a value such as "100.0 Percent" or "45.0 Minutes".
std::optional<double> leadingNumber(const QString& value);

struct Status {
    DeviceStatus status;
    bool commLost = false;  // apcupsd lost contact with the UPS (data is stale)
};

Status statusFromValues(const QMap<QString, QString>& values);

}  // namespace nutty::apcupsd
