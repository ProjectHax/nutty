// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/sources/apcupsd/ApcupsdProtocol.h"

#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace nutty::apcupsd {

QByteArray encodeRecord(QByteArrayView payload)
{
    const auto size = static_cast<quint16>(std::min<qsizetype>(payload.size(), 0xFFFF));
    QByteArray out;
    out.reserve(size + 2);
    out.append(static_cast<char>((size >> 8) & 0xFF));
    out.append(static_cast<char>(size & 0xFF));
    out.append(payload.first(size));
    return out;
}

void RecordReader::append(QByteArrayView bytes)
{
    m_buffer.append(bytes);
    while (!m_complete && m_buffer.size() >= 2) {
        const auto hi = static_cast<quint8>(m_buffer.at(0));
        const auto lo = static_cast<quint8>(m_buffer.at(1));
        const int size = (hi << 8) | lo;
        if (size == 0) {
            m_complete = true;
            m_buffer.remove(0, 2);
            break;
        }
        if (m_buffer.size() < size + 2)
            break;
        m_records << m_buffer.mid(2, size);
        m_buffer.remove(0, size + 2);
    }
}

void RecordReader::clear()
{
    m_buffer.clear();
    m_records.clear();
    m_complete = false;
}

QMap<QString, QString> parseStatusRecords(const QList<QByteArray>& records)
{
    QMap<QString, QString> values;
    for (const QByteArray& record : records) {
        const QString line = QString::fromUtf8(record).trimmed();
        const qsizetype colon = line.indexOf(u':');
        if (colon <= 0)
            continue;
        const QString key = line.left(colon).trimmed();
        const QString value = line.mid(colon + 1).trimmed();
        if (!key.isEmpty())
            values.insert(key, value);
    }
    return values;
}

std::optional<double> leadingNumber(const QString& value)
{
    static const QRegularExpression re(QStringLiteral(R"(^\s*(-?\d+(?:\.\d+)?))"));
    const auto m = re.match(value);
    if (!m.hasMatch())
        return std::nullopt;
    bool ok = false;
    const double v = m.captured(1).toDouble(&ok);
    return ok ? std::optional<double>(v) : std::nullopt;
}

Status statusFromValues(const QMap<QString, QString>& values)
{
    Status out;
    DeviceStatus& s = out.status;
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        s.raw.insert(it.key(), it.value());

    const auto num = [&](const char* key) { return leadingNumber(values.value(QString::fromLatin1(key))); };

    // STATUS is a space-separated list, e.g. "ONLINE", "ONBATT LOWBATT",
    // "ONLINE REPLACEBATT", "COMMLOST", "SHUTTING DOWN".
    const QString statusText = values.value(QStringLiteral("STATUS")).toUpper();
    const QStringList tokens = statusText.split(u' ', Qt::SkipEmptyParts);
    StatusFlags flags;
    for (const QString& t : tokens) {
        if (t == u"ONLINE")
            flags |= StatusFlag::Online;
        else if (t == u"ONBATT")
            flags |= StatusFlag::OnBattery | StatusFlag::Discharging;
        else if (t == u"LOWBATT")
            flags |= StatusFlag::LowBattery;
        else if (t == u"REPLACEBATT")
            flags |= StatusFlag::ReplaceBattery;
        else if (t == u"OVERLOAD")
            flags |= StatusFlag::Overload;
        else if (t == u"CAL")
            flags |= StatusFlag::Calibrating;
        else if (t == u"TRIM")
            flags |= StatusFlag::Trim;
        else if (t == u"BOOST")
            flags |= StatusFlag::Boost;
        else if (t == u"COMMLOST")
            out.commLost = true;
    }
    if (statusText.contains(u"SHUTTING DOWN"))
        flags |= StatusFlag::ForcedShutdown;

    s.charge = num("BCHARGE");
    if (const auto minutes = num("TIMELEFT"))
        s.runtime = static_cast<qint64>(*minutes * 60.0 + 0.5);
    s.load = num("LOADPCT");
    s.inputVoltage = num("LINEV");
    s.outputVoltage = num("OUTPUTV");
    s.batteryVoltage = num("BATTV");
    s.temperature = num("ITEMP");
    s.chargeLow = num("MBATTCHG");
    if (const auto minutes = num("MINTIMEL"))
        s.runtimeLow = static_cast<qint64>(*minutes * 60.0 + 0.5);
    if (const auto nominal = num("NOMPOWER"); nominal && s.load)
        s.wattsOut = *nominal * *s.load / 100.0;

    // apcupsd doesn't report charging; on line power below full, it is.
    if (flags.testFlag(StatusFlag::Online) && s.charge && *s.charge < 99.5)
        flags |= StatusFlag::Charging;
    s.flags = flags;

    s.manufacturer = QStringLiteral("APC");
    s.model = values.value(QStringLiteral("MODEL"));
    s.serial = values.value(QStringLiteral("SERIALNO"));
    return out;
}

}  // namespace nutty::apcupsd
