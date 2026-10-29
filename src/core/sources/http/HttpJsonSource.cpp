// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/sources/http/HttpJsonSource.h"

#include "core/sources/http/JsonPath.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

namespace nutty {

namespace {

QStringList valueList(const QVariantMap& settings, const char* key)
{
    QStringList out;
    const auto parts = settings.value(QLatin1StringView(key)).toString().split(u',', Qt::SkipEmptyParts);
    for (const auto& p : parts) {
        const QString v = p.trimmed().toLower();
        if (!v.isEmpty())
            out << v;
    }
    return out;
}

double runtimeFactor(QString unit)
{
    unit = unit.trimmed().toLower();
    if (unit == u"min" || unit == u"m" || unit == u"minutes" || unit == u"minute")
        return 60.0;
    if (unit == u"h" || unit == u"hours" || unit == u"hour")
        return 3600.0;
    if (unit == u"d" || unit == u"days")
        return 86400.0;
    return 1.0;  // seconds
}

SourceError errorForHttp(int status)
{
    if (status == 401 || status == 403)
        return SourceError::Auth;
    if (status == 404)
        return SourceError::NotFound;
    return SourceError::Network;
}

}  // namespace

HttpJsonSource::HttpJsonSource(DeviceConfig config, QObject* parent)
    : Source(std::move(config), parent)
    , m_nam(new QNetworkAccessManager(this))
{
    const auto& settings = this->config().settings;
    m_homeAssistant = settings.value(QLatin1StringView(keys::HttpPreset)).toString() == u"homeassistant";
    m_url = settings.value(QLatin1StringView(keys::HttpUrl)).toString().trimmed();
    m_insecureTls = settings.value(QLatin1StringView(keys::HttpInsecureTls)).toBool();

    bool haveAuth = false;
    const auto lines
        = settings.value(QLatin1StringView(keys::HttpHeaders)).toString().split(u'\n', Qt::SkipEmptyParts);
    for (const auto& line : lines) {
        const qsizetype colon = line.indexOf(u':');
        if (colon <= 0)
            continue;
        const QByteArray name = line.left(colon).trimmed().toUtf8();
        const QByteArray value = line.mid(colon + 1).trimmed().toUtf8();
        haveAuth = haveAuth || name.compare("authorization", Qt::CaseInsensitive) == 0;
        m_headers.append({ name, value });
    }
    const QString token
        = this->config().secrets.value(QLatin1StringView(keys::HttpToken)).toString().trimmed();
    if (!token.isEmpty() && !haveAuth)
        m_headers.append({ "Authorization", "Bearer " + token.toUtf8() });

    connect(&m_pollTimer, &QTimer::timeout, this, &HttpJsonSource::poll);
}

HttpJsonSource::~HttpJsonSource()
{
    m_pollTimer.stop();
}

QStringList HttpJsonSource::fieldNames()
{
    return {
        QString::fromLatin1(keys::HttpCharge),
        QString::fromLatin1(keys::HttpRuntime),
        QString::fromLatin1(keys::HttpLoad),
        QString::fromLatin1(keys::HttpWattsIn),
        QString::fromLatin1(keys::HttpWattsOut),
        QString::fromLatin1(keys::HttpInputVoltage),
        QString::fromLatin1(keys::HttpOutputVoltage),
        QString::fromLatin1(keys::HttpTemperature),
        QString::fromLatin1(keys::HttpStatus),
    };
}

QString HttpJsonSource::describe() const
{
    const QUrl url(m_url);
    return url.host().isEmpty() ? m_url : url.host() + url.path();
}

void HttpJsonSource::start()
{
    m_running = true;
    if (!QUrl(m_url).isValid() || QUrl(m_url).scheme().isEmpty()) {
        reportError(SourceError::Config, tr("A valid URL is required"));
        return;
    }
    setConnectionState(ConnectionState::Connecting);
    m_pollTimer.start(pollIntervalMs());
    poll();
}

void HttpJsonSource::stop()
{
    m_running = false;
    m_pollTimer.stop();
    setConnectionState(ConnectionState::Disconnected);
}

void HttpJsonSource::pollNow()
{
    poll();
}

void HttpJsonSource::poll()
{
    if (!m_running || m_pending > 0)
        return;
    if (m_homeAssistant)
        pollHomeAssistant();
    else
        pollGeneric();
}

QNetworkRequest HttpJsonSource::makeRequest(const QUrl& url) const
{
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    for (const auto& [name, value] : m_headers)
        request.setRawHeader(name, value);
    request.setTransferTimeout(kTimeoutMs);
    // Qt keeps an HTTP/2 connection open when a request on it times out, so
    // a server that stops serving a connection without closing it stalls
    // every poll after. Over HTTP/1.1 the timed-out request takes its
    // connection down and the next one starts afresh.
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    return request;
}

QNetworkReply* HttpJsonSource::send(const QUrl& url)
{
    QNetworkReply* reply = m_nam->get(makeRequest(url));
    if (m_insecureTls)
        connect(reply, &QNetworkReply::sslErrors, reply, [reply] { reply->ignoreSslErrors(); });
    return reply;
}

void HttpJsonSource::pollGeneric()
{
    m_pending = 1;
    QNetworkReply* reply = send(QUrl(m_url));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        m_pending = 0;
        if (!m_running)
            return;
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (http >= 400) {
            reportError(errorForHttp(http), tr("The server answered HTTP %1").arg(http));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            const bool tls = reply->error() == QNetworkReply::SslHandshakeFailedError;
            reportError(tls ? SourceError::Tls : SourceError::Network, reply->errorString());
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            reportError(SourceError::Protocol,
                tr("The response is not valid JSON: %1").arg(parseError.errorString()));
            return;
        }
        const QJsonValue root = doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
        QHash<QString, QJsonValue> values;
        for (const QString& field : fieldNames()) {
            const QString path = config().settings.value(field).toString().trimmed();
            if (path.isEmpty())
                continue;
            if (const auto v = json::resolve(root, path))
                values.insert(field, *v);
        }
        publish(buildStatus(values, config().settings));
    });
}

void HttpJsonSource::pollHomeAssistant()
{
    QString base = m_url;
    while (base.endsWith(u'/'))
        base.chop(1);

    m_haValues.clear();
    m_haRuntimeUnit.clear();
    m_haErrorKind = SourceError::None;
    m_haError.clear();

    QList<std::pair<QString, QString>> requests;
    for (const QString& field : fieldNames()) {
        const QString entity = config().settings.value(field).toString().trimmed();
        if (!entity.isEmpty())
            requests.append({ field, entity });
    }
    if (requests.isEmpty()) {
        reportError(SourceError::Config, tr("No Home Assistant entities are mapped"));
        return;
    }

    m_pending = static_cast<int>(requests.size());
    for (const auto& [field, entity] : requests) {
        QNetworkReply* reply = send(QUrl(base + QStringLiteral("/api/states/") + entity));
        connect(reply, &QNetworkReply::finished, this, [this, reply, field] {
            reply->deleteLater();
            const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (http >= 400 || reply->error() != QNetworkReply::NoError) {
                // Keep the most meaningful error: auth beats everything else.
                const SourceError kind = http >= 400 ? errorForHttp(http) : SourceError::Network;
                if (m_haErrorKind == SourceError::None || kind == SourceError::Auth) {
                    m_haErrorKind = kind;
                    m_haError = http >= 400 ? tr("Home Assistant answered HTTP %1").arg(http)
                                            : reply->errorString();
                }
            } else {
                const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
                const QString state = o.value(u"state").toString();
                if (!state.isEmpty() && state != u"unavailable" && state != u"unknown") {
                    m_haValues.insert(field, state);
                    if (field == QLatin1StringView(keys::HttpRuntime))
                        m_haRuntimeUnit
                            = o.value(u"attributes").toObject().value(u"unit_of_measurement").toString();
                }
            }
            if (--m_pending == 0)
                finishHomeAssistant();
        });
    }
}

void HttpJsonSource::finishHomeAssistant()
{
    if (!m_running)
        return;
    if (m_haErrorKind == SourceError::Auth || (m_haValues.isEmpty() && m_haErrorKind != SourceError::None)) {
        reportError(m_haErrorKind, m_haError);
        return;
    }
    if (m_haValues.isEmpty()) {
        reportError(SourceError::DataStale, tr("All mapped Home Assistant entities are unavailable"));
        return;
    }
    publish(buildStatus(m_haValues, config().settings, m_haRuntimeUnit));
}

DeviceStatus HttpJsonSource::buildStatus(
    const QHash<QString, QJsonValue>& values, const QVariantMap& settings, const QString& runtimeUnitHint)
{
    DeviceStatus s;
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        s.raw.insert(it.key(), json::toText(it.value()));

    const auto num = [&](const char* key) -> std::optional<double> {
        const auto it = values.constFind(QString::fromLatin1(key));
        return it == values.cend() ? std::nullopt : json::toNumber(*it);
    };

    s.charge = num(keys::HttpCharge);
    if (const auto runtime = num(keys::HttpRuntime)) {
        QString unit = settings.value(QLatin1StringView(keys::HttpRuntimeUnit)).toString();
        if (unit.isEmpty())
            unit = runtimeUnitHint;
        s.runtime = static_cast<qint64>(*runtime * runtimeFactor(unit) + 0.5);
    }
    s.load = num(keys::HttpLoad);
    s.wattsIn = num(keys::HttpWattsIn);
    s.wattsOut = num(keys::HttpWattsOut);
    s.inputVoltage = num(keys::HttpInputVoltage);
    s.outputVoltage = num(keys::HttpOutputVoltage);
    s.temperature = num(keys::HttpTemperature);

    const auto statusIt = values.constFind(QString::fromLatin1(keys::HttpStatus));
    if (statusIt == values.cend()) {
        s.flags = StatusFlag::Online;
        return s;
    }

    const QString state = json::toText(*statusIt).trimmed().toLower();
    const QStringList onBattery = valueList(settings, keys::HttpOnBatteryValues);
    const QStringList online = valueList(settings, keys::HttpOnlineValues);
    const QStringList charging = valueList(settings, keys::HttpChargingValues);
    const QStringList low = valueList(settings, keys::HttpLowBatteryValues);

    StatusFlags flags;
    if (onBattery.isEmpty() && online.isEmpty() && charging.isEmpty() && low.isEmpty()) {
        // No value lists configured: accept NUT-style tokens ("OB DISCHRG"),
        // otherwise assume mains power.
        flags = parseStatusTokens(state.toUpper());
        s.flags = flags ? flags : StatusFlags(StatusFlag::Online);
        return s;
    }
    if (onBattery.contains(state))
        flags |= StatusFlag::OnBattery | StatusFlag::Discharging;
    else if (online.contains(state) || online.isEmpty())
        flags |= StatusFlag::Online;
    if (charging.contains(state)) {
        flags |= StatusFlag::Charging;
        if (!flags.testFlag(StatusFlag::OnBattery))
            flags |= StatusFlag::Online;
    }
    if (low.contains(state))
        flags |= StatusFlag::LowBattery;
    s.flags = flags;
    return s;
}

}  // namespace nutty
