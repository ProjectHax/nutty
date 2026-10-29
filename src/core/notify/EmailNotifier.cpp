// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/notify/EmailNotifier.h"

#include "core/notify/NotifyUtil.h"

#include <QRegularExpression>

namespace nutty {

EmailNotifier::EmailNotifier(ChannelConfig config, QObject* parent)
    : Notifier(std::move(config), parent)
    , m_client(new SmtpClient(this))
{
    connect(m_client, &SmtpClient::finished, this, [this](bool ok, const QString& error) {
        if (ok)
            emit sent(channelId());
        else
            emit failed(channelId(), error);
        sendNext();
    });
}

SmtpClient::Settings EmailNotifier::smtpSettings(const ChannelConfig& config)
{
    const auto& settings = config.settings;
    SmtpClient::Settings out;
    out.host = settings.value(QString::fromLatin1(keys::EmailHost)).toString().trimmed();
    const auto security
        = settings.value(QString::fromLatin1(keys::EmailSecurity), QStringLiteral("starttls")).toString();
    if (security == u"none")
        out.security = SmtpClient::Security::None;
    else if (security == u"tls")
        out.security = SmtpClient::Security::Tls;
    else
        out.security = SmtpClient::Security::StartTls;
    const quint16 defaultPort = out.security == SmtpClient::Security::None ? 25
        : out.security == SmtpClient::Security::Tls                        ? 465
                                                                           : 587;
    const int port = settings.value(QString::fromLatin1(keys::EmailPort)).toInt();
    out.port = port > 0 && port <= 65535 ? quint16(port) : defaultPort;
    out.username = settings.value(QString::fromLatin1(keys::EmailUsername)).toString().trimmed();
    out.password = config.secrets.value(QString::fromLatin1(keys::EmailPassword)).toString();
    return out;
}

SmtpClient::Message EmailNotifier::message(const ChannelConfig& config, const Event& event)
{
    const auto& settings = config.settings;
    SmtpClient::Message out;
    out.from = settings.value(QString::fromLatin1(keys::EmailFrom)).toString().trimmed();
    if (out.from.isEmpty()) {
        const auto username = settings.value(QString::fromLatin1(keys::EmailUsername)).toString().trimmed();
        out.from = username.contains(u'@') ? username : QStringLiteral("nutty@localhost");
    }
    static const QRegularExpression separators(QStringLiteral("[,;]"));
    for (const auto& to :
        settings.value(QString::fromLatin1(keys::EmailTo)).toString().split(separators, Qt::SkipEmptyParts)) {
        if (!to.trimmed().isEmpty())
            out.to << to.trimmed();
    }
    out.subject = QStringLiteral("[nutty] %1").arg(event.title);
    out.body = notify::describeEvent(event);
    out.date = event.timestamp;
    return out;
}

void EmailNotifier::send(const Event& event)
{
    m_queue.enqueue(event);
    if (!m_client->isBusy())
        sendNext();
}

void EmailNotifier::sendNext()
{
    if (m_queue.isEmpty() || m_client->isBusy())
        return;
    const auto event = m_queue.dequeue();
    m_client->send(smtpSettings(config()), message(config(), event));
}

}  // namespace nutty
