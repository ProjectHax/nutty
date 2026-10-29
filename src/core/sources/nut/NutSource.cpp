// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/sources/nut/NutSource.h"

#include <QMap>

#include <algorithm>
#include <iterator>

namespace nutty {

NutSource::NutSource(DeviceConfig config, QObject* parent)
    : Source(std::move(config), parent)
{
    m_options = nut::SessionOptions::fromSettings(this->config().settings, this->config().secrets);
    m_ups = this->config().settings.value(QLatin1StringView(keys::NutUps)).toString().trimmed();
    m_secondary = this->config().settings.value(QLatin1StringView(keys::NutSecondary)).toBool();

    connect(&m_pollTimer, &QTimer::timeout, this, &NutSource::poll);
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &NutSource::connectSession);
}

NutSource::~NutSource()
{
    stop();
}

QString NutSource::describe() const
{
    return QStringLiteral("%1@%2:%3").arg(m_ups, m_options.host).arg(m_options.port);
}

void NutSource::start()
{
    m_running = true;
    m_shuttingDown = false;
    m_backoffIndex = 0;
    if (m_ups.isEmpty()) {
        reportError(SourceError::Config, tr("No UPS name is configured"));
        return;
    }
    connectSession();
}

void NutSource::stop()
{
    m_running = false;
    m_pollTimer.stop();
    m_reconnectTimer.stop();
    m_polling = false;
    m_loggedIn = false;
    if (m_session) {
        m_session->close();
        m_session->deleteLater();
        m_session = nullptr;
    }
    setConnectionState(ConnectionState::Disconnected);
}

void NutSource::pollNow()
{
    if (m_session && m_session->isReady()) {
        poll();
    } else if (m_running && !m_shuttingDown && !m_session) {
        m_reconnectTimer.stop();
        connectSession();
    }
}

void NutSource::prepareForShutdown()
{
    // From here on, never reconnect: logging in again would make the primary
    // wait for this machine all over again.
    m_shuttingDown = true;
    m_reconnectTimer.stop();
    m_pollTimer.stop();
    if (m_session && m_loggedIn)
        m_session->sendAndForget({ QStringLiteral("LOGOUT") });
    m_loggedIn = false;
}

void NutSource::connectSession()
{
    if (!m_running || m_shuttingDown)
        return;
    if (m_session) {
        m_session->close();
        m_session->deleteLater();
    }
    m_polling = false;
    m_loggedIn = false;
    setConnectionState(ConnectionState::Connecting);

    m_session = new nut::NutSession(this);
    connect(m_session, &nut::NutSession::ready, this, &NutSource::onSessionReady);
    connect(m_session, &nut::NutSession::failed, this, &NutSource::onSessionFailed);
    m_session->open(m_options);
}

void NutSource::onSessionReady()
{
    if (!m_secondary) {
        startPolling();
        return;
    }
    // A secondary logs in to the UPS (again after every reconnect) so the
    // primary counts it and waits for it during a forced shutdown.
    m_session->sendSimple({ QStringLiteral("LOGIN"), m_ups }, [this](const nut::Line& reply) {
        if (reply.kind == nut::LineKind::Ok || reply.errorCode == u"ALREADY-LOGGED-IN") {
            m_loggedIn = true;
        } else if (reply.kind == nut::LineKind::Err) {
            // Keep monitoring even when the account lacks the upsmon role,
            // but make the problem visible.
            reportError(nut::errorKind(reply.errorCode),
                tr("Could not log in as a secondary: %1").arg(nut::errorDescription(reply.errorCode)));
        }
        startPolling();
    });
}

void NutSource::onSessionFailed(SourceError kind, const QString& message)
{
    m_pollTimer.stop();
    m_polling = false;
    m_loggedIn = false;
    if (m_session) {
        m_session->deleteLater();
        m_session = nullptr;
    }
    if (m_shuttingDown) {
        setConnectionState(ConnectionState::Disconnected);
        return;
    }
    reportError(kind, message);
    scheduleReconnect();
}

void NutSource::startPolling()
{
    m_pollTimer.start(pollIntervalMs());
    poll();
}

void NutSource::poll()
{
    if (!m_session || !m_session->isReady() || m_polling)
        return;
    m_polling = true;
    m_session->sendList({ QStringLiteral("LIST"), QStringLiteral("VAR"), m_ups },
        [this](const std::optional<QString>& error, const QList<QStringList>& items) {
            m_polling = false;
            if (error) {
                // The connection is fine; the server just can't
                // give us data right now (stale, unknown UPS, ...).
                reportError(nut::errorKind(*error), nut::errorDescription(*error));
                return;
            }
            QMap<QString, QString> vars;
            for (const QStringList& words : items) {
                // VAR <ups> <name> <value>
                if (words.size() >= 4 && words.at(0) == u"VAR")
                    vars.insert(words.at(2), words.at(3));
            }
            m_backoffIndex = 0;
            publish(nut::statusFromVariables(vars));
        });
}

void NutSource::scheduleReconnect()
{
    if (!m_running || m_shuttingDown)
        return;
    const int last = static_cast<int>(std::size(kBackoffMs)) - 1;
    const int delay = kBackoffMs[std::min(m_backoffIndex, last)];
    m_backoffIndex = std::min(m_backoffIndex + 1, last);
    m_reconnectTimer.start(delay);
}

// --- NutDiscovery -------------------------------------------------------------

NutDiscovery::NutDiscovery(const QVariantMap& settings, const QVariantMap& secrets, QObject* parent)
    : Discovery(parent)
{
    m_session = new nut::NutSession(this);
    connect(m_session, &nut::NutSession::failed, this,
        [this](SourceError, const QString& message) { fail(message); });
    connect(m_session, &nut::NutSession::ready, this, [this] {
        m_session->sendList({ QStringLiteral("LIST"), QStringLiteral("UPS") },
            [this](const std::optional<QString>& error, const QList<QStringList>& items) {
                if (error) {
                    fail(nut::errorDescription(*error));
                    return;
                }
                QList<DiscoveredDevice> found;
                for (const QStringList& words : items) {
                    // UPS <name> <description>
                    if (words.size() < 2 || words.at(0) != u"UPS")
                        continue;
                    DiscoveredDevice d;
                    d.id = words.at(1);
                    d.name = words.at(1);
                    d.description = words.value(2);
                    found << d;
                }
                m_session->close();
                finish(found);
            });
    });
    m_session->open(nut::SessionOptions::fromSettings(settings, secrets));
}

}  // namespace nutty
