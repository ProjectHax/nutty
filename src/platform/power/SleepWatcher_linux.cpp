// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/power/SleepWatcher.h"

#include <QDBusConnection>

namespace nutty {

namespace {

// Receives logind's PrepareForSleep(bool start) signal. QtDBus needs a real
// slot to connect to, so this small QObject stays out of the public header.
class LogindSleepListener : public QObject
{
    Q_OBJECT

public:
    explicit LogindSleepListener(SleepWatcher* watcher)
        : QObject(watcher)
        , m_watcher(watcher)
    {
        m_connected = QDBusConnection::systemBus().connect(QStringLiteral("org.freedesktop.login1"),
            QStringLiteral("/org/freedesktop/login1"), QStringLiteral("org.freedesktop.login1.Manager"),
            QStringLiteral("PrepareForSleep"), this, SLOT(onPrepareForSleep(bool)));
    }

    bool isConnected() const { return m_connected; }

public slots:
    void onPrepareForSleep(bool start)
    {
        if (start)
            m_watcher->notifySleeping();
        else
            m_watcher->notifyResumed();
    }

private:
    SleepWatcher* m_watcher;
    bool m_connected = false;
};

}  // namespace

struct SleepWatcher::Private {
    LogindSleepListener* listener = nullptr;
};

SleepWatcher::SleepWatcher(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->listener = new LogindSleepListener(this);
}

SleepWatcher::~SleepWatcher() = default;

bool SleepWatcher::isSupported() const
{
    return d->listener && d->listener->isConnected();
}

}  // namespace nutty

#include "SleepWatcher_linux.moc"
