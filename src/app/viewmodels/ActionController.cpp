// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "app/viewmodels/ActionController.h"

#include "app/viewmodels/AppContext.h"
#include "core/engine/EventText.h"
#include "core/engine/Monitor.h"
#include "platform/power/PowerActions.h"

namespace nutty {

namespace {
QString lowerKey(ActionType type)
{
    return enumKey(type).toLower();
}
}  // namespace

ActionController::ActionController(AppContext* context, QObject* parent)
    : QObject(parent)
    , m_context(context ? context : AppContext::instance())
{
    m_ticker.setInterval(250);
    connect(&m_ticker, &QTimer::timeout, this, [this] {
        const int r = remaining();
        if (r != m_lastRemaining) {
            m_lastRemaining = r;
            emit remainingChanged();
        }
    });
    if (!m_context)
        return;
    Monitor* monitor = m_context->monitor();
    connect(monitor, &Monitor::pendingActionChanged, this, &ActionController::onPendingChanged);
    connect(monitor, &Monitor::snoozeChanged, this, &ActionController::snoozeChanged);
    onPendingChanged();
}

ActionController* ActionController::create(QQmlEngine*, QJSEngine*)
{
    return new ActionController(AppContext::instance());
}

void ActionController::onPendingChanged()
{
    if (pending())
        m_ticker.start();
    else
        m_ticker.stop();
    m_lastRemaining = remaining();
    emit pendingChanged();
    emit remainingChanged();
}

#define NUTTY_PENDING_OR(fallback)                                                                           \
    const auto p = m_context ? m_context->monitor()->pendingAction() : std::nullopt;                         \
    if (!p)                                                                                                  \
    return fallback

bool ActionController::pending() const
{
    return m_context && m_context->monitor()->pendingAction().has_value();
}

QString ActionController::action() const
{
    NUTTY_PENDING_OR(QString());
    return lowerKey(p->type);
}

QString ActionController::actionName() const
{
    NUTTY_PENDING_OR(QString());
    return text::actionName(p->type);
}

QString ActionController::title() const
{
    NUTTY_PENDING_OR(QString());
    return countdownTitle(lowerKey(p->type), remaining(), p->dryRun || dryRun());
}

QString ActionController::deviceName() const
{
    NUTTY_PENDING_OR(QString());
    return p->deviceName;
}

QString ActionController::targetName() const
{
    NUTTY_PENDING_OR(QString());
    return p->targetName;
}

QString ActionController::ruleName() const
{
    NUTTY_PENDING_OR(QString());
    return p->ruleName;
}

QString ActionController::reason() const
{
    NUTTY_PENDING_OR(QString());
    QString r = p->reason;
    if (!r.isEmpty())
        r[0] = r[0].toUpper();
    return r;
}

int ActionController::remaining() const
{
    NUTTY_PENDING_OR(0);
    return static_cast<int>(p->remainingSec(m_context->monitor()->now()));
}

int ActionController::total() const
{
    NUTTY_PENDING_OR(0);
    return static_cast<int>(std::max<qint64>(1, p->scheduledAt.secsTo(p->deadline)));
}

bool ActionController::cancellable() const
{
    NUTTY_PENDING_OR(false);
    return p->cancellable && !p->fsd;
}

bool ActionController::fsd() const
{
    NUTTY_PENDING_OR(false);
    return p->fsd;
}

bool ActionController::simulation() const
{
    NUTTY_PENDING_OR(false);
    return p->dryRun || dryRun();
}

#undef NUTTY_PENDING_OR

bool ActionController::dryRun() const
{
    return m_context && m_context->monitor()->dryRun();
}

void ActionController::setDryRun(bool enabled)
{
    if (!m_context || enabled == dryRun() || (dryRunForced() && !enabled))
        return;
    m_context->monitor()->setDryRun(enabled);
    emit dryRunChanged();
    emit pendingChanged();
}

bool ActionController::dryRunForced() const
{
    return m_context && m_context->dryRunForced();
}

bool ActionController::snoozed() const
{
    if (!m_context)
        return false;
    const QDateTime until = m_context->monitor()->snoozedUntil();
    return until.isValid() && until > m_context->monitor()->now();
}

QDateTime ActionController::snoozedUntil() const
{
    return snoozed() ? m_context->monitor()->snoozedUntil() : QDateTime();
}

bool ActionController::cancel()
{
    return m_context && m_context->monitor()->cancelPendingAction();
}

void ActionController::snooze(int minutes)
{
    if (m_context)
        m_context->monitor()->snoozeActions(minutes * 60);
}

void ActionController::resume()
{
    if (m_context)
        m_context->monitor()->clearSnooze();
}

bool ActionController::simulate(const QString& ruleId)
{
    return m_context && m_context->monitor()->simulateRule(parseId(ruleId));
}

QVariantMap ActionController::capability(const QString& action) const
{
    const auto type
        = enumFromKey(action.isEmpty() ? action : action.at(0).toUpper() + action.mid(1), ActionType::Notify);
    const PowerCapability cap = PowerActions::capability(type);
    return { { QStringLiteral("available"), cap.available }, { QStringLiteral("reason"), cap.reason } };
}

bool ActionController::shutdownNeedsPermission() const
{
#ifdef Q_OS_MACOS
    return true;
#else
    return false;
#endif
}

QString ActionController::requestShutdownPermission()
{
    const auto result = PowerActions::requestShutdownPermission();
    return result ? QString() : result.error();
}

QString ActionController::countdownTitle(const QString& action, int remaining, bool simulation)
{
    const QString when = text::duration(remaining);
    QString title;
    if (action == u"sleep")
        title = tr("Going to sleep in %1").arg(when);
    else if (action == u"hibernate")
        title = tr("Hibernating in %1").arg(when);
    else if (action == u"command")
        title = tr("Running your command in %1").arg(when);
    else if (action == u"notify")
        title = tr("Notifying in %1").arg(when);
    else
        title = tr("Shutting down in %1").arg(when);
    return simulation ? tr("Simulation: %1").arg(title) : title;
}

}  // namespace nutty
