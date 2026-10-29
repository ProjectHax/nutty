// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/notify/NotifyUtil.h"

#include <QCoreApplication>
#include <QLocale>
#include <QRegularExpression>
#include <QUrl>

namespace nutty::notify {

QString severityKey(Severity severity)
{
    switch (severity) {
    case Severity::Info:
        return QStringLiteral("info");
    case Severity::Warning:
        return QStringLiteral("warning");
    case Severity::Critical:
        return QStringLiteral("critical");
    }
    return QStringLiteral("info");
}

QList<QPair<QByteArray, QByteArray>> parseHeaderLines(const QString& text)
{
    QList<QPair<QByteArray, QByteArray>> headers;
    const auto lines = QStringView(text).split(u'\n');
    for (const auto line : lines) {
        const auto colon = line.indexOf(u':');
        if (colon <= 0)
            continue;
        const auto name = line.left(colon).trimmed();
        const auto value = line.mid(colon + 1).trimmed();
        if (name.isEmpty() || name.contains(u' '))
            continue;
        headers.append({ name.toLatin1(), value.toUtf8() });
    }
    return headers;
}

QString jsonEscape(const QString& text)
{
    QString out;
    out.reserve(text.size() + 8);
    for (const QChar ch : text) {
        switch (ch.unicode()) {
        case u'"':
            out += QLatin1StringView("\\\"");
            break;
        case u'\\':
            out += QLatin1StringView("\\\\");
            break;
        case u'\b':
            out += QLatin1StringView("\\b");
            break;
        case u'\f':
            out += QLatin1StringView("\\f");
            break;
        case u'\n':
            out += QLatin1StringView("\\n");
            break;
        case u'\r':
            out += QLatin1StringView("\\r");
            break;
        case u'\t':
            out += QLatin1StringView("\\t");
            break;
        default:
            if (ch.unicode() < 0x20)
                out += QStringLiteral("\\u%1").arg(uint(ch.unicode()), 4, 16, QLatin1Char('0'));
            else
                out += ch;
        }
    }
    return out;
}

QHash<QString, QString> eventPlaceholders(const Event& event)
{
    QHash<QString, QString> values;
    values.insert(QStringLiteral("event"), enumKey(event.type));
    values.insert(QStringLiteral("severity"), severityKey(event.severity));
    values.insert(QStringLiteral("device"), event.deviceName);
    values.insert(QStringLiteral("device_id"),
        event.deviceId.isNull() ? QString() : event.deviceId.toString(QUuid::WithoutBraces));
    values.insert(QStringLiteral("title"), event.title);
    values.insert(QStringLiteral("message"), event.message);
    values.insert(
        QStringLiteral("charge"), event.charge ? QString::number(qRound(*event.charge)) : QString());
    values.insert(QStringLiteral("runtime"), event.runtime ? QString::number(*event.runtime) : QString());
    values.insert(QStringLiteral("runtime_min"),
        event.runtime ? QString::number(qRound(double(*event.runtime) / 60.0)) : QString());
    values.insert(QStringLiteral("load"), event.load ? QString::number(qRound(*event.load)) : QString());
    values.insert(QStringLiteral("status"), statusTokens(event.flags));
    const auto ts = event.timestamp.isValid() ? event.timestamp : QDateTime::currentDateTimeUtc();
    values.insert(QStringLiteral("timestamp"), ts.toUTC().toString(Qt::ISODate));
    values.insert(
        QStringLiteral("timestamp_local"), ts.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    return values;
}

QString renderTemplate(const QString& tmpl, const QHash<QString, QString>& values, bool jsonEscapeValues)
{
    static const QRegularExpression token(QStringLiteral("\\{([a-z_]+)\\}"));
    QString out;
    out.reserve(tmpl.size());
    qsizetype last = 0;
    auto it = token.globalMatch(tmpl);
    while (it.hasNext()) {
        const auto match = it.next();
        const auto name = match.captured(1);
        const auto value = values.constFind(name);
        if (value == values.cend())
            continue;
        out += QStringView(tmpl).mid(last, match.capturedStart() - last);
        out += jsonEscapeValues ? jsonEscape(*value) : *value;
        last = match.capturedEnd();
    }
    out += QStringView(tmpl).mid(last);
    return out;
}

QByteArray encodeHeaderWord(const QString& text)
{
    bool plain = true;
    for (const QChar ch : text) {
        if (ch.unicode() < 0x20 || ch.unicode() > 0x7e) {
            plain = false;
            break;
        }
    }
    if (plain)
        return text.toLatin1();

    // Each encoded word carries at most 45 bytes of UTF-8 (60 base64 chars),
    // keeping it under the 75-character limit, and never splits a character.
    constexpr qsizetype kMaxChunk = 45;
    QList<QByteArray> words;
    QByteArray chunk;
    for (qsizetype i = 0; i < text.size(); ++i) {
        qsizetype len = text.at(i).isHighSurrogate() && i + 1 < text.size() ? 2 : 1;
        const QByteArray bytes = text.mid(i, len).toUtf8();
        i += len - 1;
        if (chunk.size() + bytes.size() > kMaxChunk) {
            words.append(chunk);
            chunk.clear();
        }
        chunk += bytes;
    }
    if (!chunk.isEmpty())
        words.append(chunk);

    QByteArray out;
    for (qsizetype i = 0; i < words.size(); ++i) {
        if (i > 0)
            out += "\r\n ";
        out += "=?UTF-8?B?" + words.at(i).toBase64() + "?=";
    }
    return out;
}

QByteArray formEncode(const QList<QPair<QString, QString>>& fields)
{
    QByteArray out;
    for (const auto& [key, value] : fields) {
        if (!out.isEmpty())
            out += '&';
        out += QUrl::toPercentEncoding(key) + '=' + QUrl::toPercentEncoding(value);
    }
    return out;
}

QString describeEvent(const Event& event)
{
    QStringList lines;
    lines << event.message << QString();
    if (!event.deviceName.isEmpty())
        lines << QCoreApplication::translate("nutty::Notify", "Device: %1").arg(event.deviceName);
    if (event.flags)
        lines << QCoreApplication::translate("nutty::Notify", "Status: %1").arg(statusTokens(event.flags));
    if (event.charge)
        lines << QCoreApplication::translate("nutty::Notify", "Charge: %1 %").arg(qRound(*event.charge));
    if (event.runtime)
        lines << QCoreApplication::translate("nutty::Notify", "Runtime: %1 min")
                     .arg(qRound(double(*event.runtime) / 60.0));
    if (event.load)
        lines << QCoreApplication::translate("nutty::Notify", "Load: %1 %").arg(qRound(*event.load));
    const auto ts = event.timestamp.isValid() ? event.timestamp : QDateTime::currentDateTimeUtc();
    lines << QCoreApplication::translate("nutty::Notify", "Time: %1")
                 .arg(QLocale().toString(ts.toLocalTime(), QLocale::LongFormat));
    return lines.join(u'\n');
}

}  // namespace nutty::notify
