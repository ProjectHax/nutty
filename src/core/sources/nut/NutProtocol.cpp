// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/sources/nut/NutProtocol.h"

#include <QCoreApplication>

#include <optional>

namespace nutty::nut {

QStringList tokenize(QStringView line)
{
    QStringList words;
    QString current;
    bool inQuotes = false;
    bool haveWord = false;

    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (inQuotes) {
            if (c == u'\\' && i + 1 < line.size()) {
                current += line.at(++i);
            } else if (c == u'"') {
                inQuotes = false;
            } else {
                current += c;
            }
        } else if (c == u'"') {
            inQuotes = true;
            haveWord = true;
        } else if (c == u' ' || c == u'\t' || c == u'\r' || c == u'\n') {
            if (haveWord) {
                words << current;
                current.clear();
                haveWord = false;
            }
        } else {
            current += c;
            haveWord = true;
        }
    }
    if (haveWord)
        words << current;
    return words;
}

QString quote(QStringView arg)
{
    const bool needsQuotes = arg.isEmpty() || arg.contains(u' ') || arg.contains(u'"') || arg.contains(u'\\')
        || arg.contains(u'\t');
    if (!needsQuotes)
        return arg.toString();

    QString out;
    out.reserve(arg.size() + 2);
    out += u'"';
    for (const QChar c : arg) {
        if (c == u'"' || c == u'\\')
            out += u'\\';
        out += c;
    }
    out += u'"';
    return out;
}

QByteArray command(const QStringList& words)
{
    QStringList quoted;
    quoted.reserve(words.size());
    for (const auto& w : words)
        quoted << quote(w);
    return quoted.join(u' ').toUtf8() + '\n';
}

Line parseLine(QStringView text)
{
    Line line;
    line.words = tokenize(text);
    if (line.words.isEmpty())
        return line;

    const QString& head = line.words.first();
    if (head == u"OK") {
        line.kind = LineKind::Ok;
    } else if (head == u"ERR") {
        line.kind = LineKind::Err;
        line.errorCode = line.words.value(1);
    } else if (head == u"BEGIN" && line.words.value(1) == u"LIST") {
        line.kind = LineKind::Begin;
    } else if (head == u"END" && line.words.value(1) == u"LIST") {
        line.kind = LineKind::End;
    } else if (head == u"VAR" && line.words.size() >= 4) {
        line.kind = LineKind::Var;
    } else if (head == u"UPS" && line.words.size() >= 2) {
        line.kind = LineKind::Ups;
    }
    return line;
}

SourceError errorKind(QStringView code, bool duringStartTls)
{
    if (code == u"ACCESS-DENIED" || code == u"USERNAME-REQUIRED" || code == u"PASSWORD-REQUIRED"
        || code == u"INVALID-USERNAME" || code == u"INVALID-PASSWORD" || code == u"ALREADY-SET-USERNAME"
        || code == u"ALREADY-SET-PASSWORD")
        return SourceError::Auth;
    if (code == u"UNKNOWN-UPS")
        return SourceError::NotFound;
    if (code == u"DATA-STALE" || code == u"DRIVER-NOT-CONNECTED")
        return SourceError::DataStale;
    if (code.startsWith(u"FEATURE-") || code == u"ALREADY-SSL-MODE")
        return duringStartTls ? SourceError::Tls : SourceError::Protocol;
    return SourceError::Protocol;
}

QString errorDescription(QStringView code)
{
    const char* text = nullptr;
    if (code == u"ACCESS-DENIED")
        text = QT_TRANSLATE_NOOP("nutty::nut", "Access denied by the NUT server");
    else if (code == u"UNKNOWN-UPS")
        text = QT_TRANSLATE_NOOP("nutty::nut", "The NUT server doesn't know this UPS");
    else if (code == u"DATA-STALE")
        text = QT_TRANSLATE_NOOP("nutty::nut", "The UPS data on the server is stale");
    else if (code == u"DRIVER-NOT-CONNECTED")
        text = QT_TRANSLATE_NOOP("nutty::nut", "The NUT driver isn't connected to the UPS");
    else if (code == u"INVALID-USERNAME" || code == u"USERNAME-REQUIRED")
        text = QT_TRANSLATE_NOOP("nutty::nut", "Invalid or missing NUT username");
    else if (code == u"INVALID-PASSWORD" || code == u"PASSWORD-REQUIRED")
        text = QT_TRANSLATE_NOOP("nutty::nut", "Invalid or missing NUT password");
    else if (code == u"FEATURE-NOT-SUPPORTED" || code == u"FEATURE-NOT-CONFIGURED")
        text = QT_TRANSLATE_NOOP("nutty::nut", "The NUT server doesn't support TLS (STARTTLS)");
    else if (code == u"ALREADY-LOGGED-IN")
        text = QT_TRANSLATE_NOOP("nutty::nut", "Already logged in to this UPS");

    if (text)
        return QCoreApplication::translate("nutty::nut", text);
    return QCoreApplication::translate("nutty::nut", "NUT server error: %1").arg(code);
}

namespace {

std::optional<double> number(const QMap<QString, QString>& vars, const char* key)
{
    const auto it = vars.constFind(QString::fromLatin1(key));
    if (it == vars.cend())
        return std::nullopt;
    bool ok = false;
    const double value = it->trimmed().toDouble(&ok);
    return ok ? std::optional<double>(value) : std::nullopt;
}

std::optional<double> firstNumber(const QMap<QString, QString>& vars, std::initializer_list<const char*> keys)
{
    for (const char* key : keys) {
        if (auto v = number(vars, key))
            return v;
    }
    return std::nullopt;
}

QString firstText(const QMap<QString, QString>& vars, std::initializer_list<const char*> keys)
{
    for (const char* key : keys) {
        const QString v = vars.value(QString::fromLatin1(key)).trimmed();
        if (!v.isEmpty())
            return v;
    }
    return {};
}

std::optional<qint64> seconds(std::optional<double> v)
{
    if (!v)
        return std::nullopt;
    return static_cast<qint64>(*v + 0.5);
}

}  // namespace

DeviceStatus statusFromVariables(const QMap<QString, QString>& vars)
{
    DeviceStatus s;
    s.raw = vars;
    s.flags = parseStatusTokens(vars.value(QStringLiteral("ups.status")));

    s.charge = number(vars, "battery.charge");
    s.runtime = seconds(number(vars, "battery.runtime"));
    s.load = number(vars, "ups.load");
    s.inputVoltage = number(vars, "input.voltage");
    s.outputVoltage = number(vars, "output.voltage");
    s.batteryVoltage = number(vars, "battery.voltage");
    s.temperature = firstNumber(vars, { "ups.temperature", "battery.temperature", "ambient.temperature" });
    s.chargeLow = number(vars, "battery.charge.low");
    s.runtimeLow = seconds(number(vars, "battery.runtime.low"));

    s.wattsOut = number(vars, "ups.realpower");
    if (!s.wattsOut) {
        const auto nominal = number(vars, "ups.realpower.nominal");
        if (nominal && s.load)
            s.wattsOut = *nominal * *s.load / 100.0;
    }
    s.wattsIn = number(vars, "input.realpower");

    s.manufacturer = firstText(vars, { "device.mfr", "ups.mfr" });
    s.model = firstText(vars, { "device.model", "ups.model" });
    s.serial = firstText(vars, { "device.serial", "ups.serial" });
    s.alarmText = vars.value(QStringLiteral("ups.alarm")).trimmed();
    return s;
}

}  // namespace nutty::nut
