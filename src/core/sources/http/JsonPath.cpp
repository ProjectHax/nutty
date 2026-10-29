// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/sources/http/JsonPath.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <cmath>

namespace nutty::json {

std::optional<QJsonValue> resolve(const QJsonValue& root, QStringView path)
{
    QJsonValue current = root;
    qsizetype i = 0;
    const qsizetype n = path.size();

    const auto member = [&](const QString& name) -> bool {
        if (!current.isObject())
            return false;
        const QJsonObject o = current.toObject();
        const auto it = o.constFind(name);
        if (it == o.constEnd())
            return false;
        current = *it;
        return true;
    };

    while (i < n) {
        const QChar c = path.at(i);
        if (c == u'.') {
            ++i;
            continue;
        }
        if (c == u'[') {
            const qsizetype close = path.indexOf(u']', i);
            if (close < 0)
                return std::nullopt;
            const QStringView inner = path.mid(i + 1, close - i - 1).trimmed();
            i = close + 1;
            if (inner.size() >= 2 && (inner.front() == u'"' || inner.front() == u'\'')
                && inner.back() == inner.front()) {
                if (!member(inner.mid(1, inner.size() - 2).toString()))
                    return std::nullopt;
                continue;
            }
            bool ok = false;
            const int index = inner.toInt(&ok);
            if (!ok || !current.isArray())
                return std::nullopt;
            const QJsonArray a = current.toArray();
            if (index < 0 || index >= a.size())
                return std::nullopt;
            current = a.at(index);
            continue;
        }
        qsizetype end = i;
        while (end < n && path.at(end) != u'.' && path.at(end) != u'[')
            ++end;
        if (!member(path.mid(i, end - i).trimmed().toString()))
            return std::nullopt;
        i = end;
    }
    return current;
}

std::optional<double> toNumber(const QJsonValue& value)
{
    if (value.isDouble())
        return value.toDouble();
    if (value.isBool())
        return value.toBool() ? 1.0 : 0.0;
    if (value.isString()) {
        QString s = value.toString().trimmed();
        // Tolerate a trailing unit ("87 %", "230 V").
        qsizetype end = 0;
        while (end < s.size()
            && (s.at(end).isDigit() || s.at(end) == u'.' || s.at(end) == u'-' || s.at(end) == u'+'
                || s.at(end) == u'e' || s.at(end) == u'E'))
            ++end;
        bool ok = false;
        const double d = s.left(end).toDouble(&ok);
        if (ok && std::isfinite(d))
            return d;
    }
    return std::nullopt;
}

QString toText(const QJsonValue& value)
{
    if (value.isString())
        return value.toString();
    if (value.isBool())
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.isDouble()) {
        const double d = value.toDouble();
        if (std::floor(d) == d && std::abs(d) < 1e15)
            return QString::number(static_cast<qint64>(d));
        return QString::number(d);
    }
    return {};
}

}  // namespace nutty::json
