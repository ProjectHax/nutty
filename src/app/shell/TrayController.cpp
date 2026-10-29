// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "app/shell/TrayController.h"

#include "app/viewmodels/ActionController.h"
#include "app/viewmodels/AppContext.h"
#include "app/viewmodels/DeviceInfo.h"
#include "core/engine/Monitor.h"

#include <QAction>
#include <QMenu>
#include <QSystemTrayIcon>

namespace nutty {

TrayController::TrayController(AppContext* context, QObject* parent)
    : QObject(parent)
    , m_context(context)
    , m_menu(std::make_unique<QMenu>())
{
    m_icon = new QSystemTrayIcon(this);
#ifdef Q_OS_MACOS
    m_state.mask = true;
#endif

    m_deviceSeparator = m_menu->addSeparator();
    m_pending = m_menu->addAction(QString());
    m_pending->setEnabled(false);
    m_cancel = m_menu->addAction(QString());
    connect(m_cancel, &QAction::triggered, this, [this] {
        if (m_context)
            m_context->monitor()->cancelPendingAction();
    });
    m_snooze = m_menu->addAction(QString());
    connect(m_snooze, &QAction::triggered, this, [this] {
        if (!m_context)
            return;
        Monitor* monitor = m_context->monitor();
        if (monitor->snoozedUntil().isValid() && monitor->snoozedUntil() > monitor->now())
            monitor->clearSnooze();
        else
            monitor->snoozeActions(3600);
    });
    m_menu->addSeparator();
    m_open = m_menu->addAction(QString());
    connect(m_open, &QAction::triggered, this, &TrayController::openRequested);
    m_quit = m_menu->addAction(QString());
    m_quit->setMenuRole(QAction::QuitRole);
    connect(m_quit, &QAction::triggered, this, &TrayController::quitRequested);
    connect(m_menu.get(), &QMenu::aboutToShow, this, [this] {
        update();
        m_ticker.start();
    });
    connect(m_menu.get(), &QMenu::aboutToHide, &m_ticker, &QTimer::stop);
    m_icon->setContextMenu(m_menu.get());

    connect(m_icon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
#ifndef Q_OS_MACOS
        // macOS opens the menu on click, as menu bar items do.
        if (reason == QSystemTrayIcon::Trigger)
            emit toggleRequested();
#endif
        if (reason == QSystemTrayIcon::DoubleClick)
            emit openRequested();
    });
    connect(m_icon, &QSystemTrayIcon::messageClicked, this, &TrayController::openRequested);

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(50);
    connect(&m_debounce, &QTimer::timeout, this, &TrayController::update);
    m_ticker.setInterval(1000);
    connect(&m_ticker, &QTimer::timeout, this, &TrayController::update);

    if (m_context) {
        Monitor* monitor = m_context->monitor();
        connect(monitor, &Monitor::devicesChanged, this, &TrayController::rebuildDeviceActions);
        connect(monitor, &Monitor::deviceStatusChanged, this, &TrayController::scheduleUpdate);
        connect(monitor, &Monitor::deviceConnectionChanged, this, &TrayController::scheduleUpdate);
        connect(monitor, &Monitor::pendingActionChanged, this, &TrayController::update);
        connect(monitor, &Monitor::snoozeChanged, this, &TrayController::update);
        connect(m_context, &AppContext::configChanged, this, &TrayController::rebuildDeviceActions);
    }
    retranslate();
    rebuildDeviceActions();
    m_icon->show();
}

TrayController::~TrayController()
{
    m_icon->hide();
    m_icon->setContextMenu(nullptr);
}

void TrayController::setStyle(TrayIconStyle style)
{
    if (style == m_state.style)
        return;
    m_state.style = style;
    update();
}

void TrayController::setMonochrome(bool monochrome)
{
    if (monochrome == m_state.monochrome)
        return;
    m_state.monochrome = monochrome;
    update();
}

void TrayController::setDarkPanel(bool dark)
{
    if (dark == m_state.darkPanel)
        return;
    m_state.darkPanel = dark;
    update();
}

void TrayController::retranslate()
{
    m_open->setText(tr("Open nutty"));
    m_cancel->setText(tr("Cancel"));
    m_quit->setText(tr("Quit"));
    update();
}

void TrayController::scheduleUpdate()
{
    m_debounce.start();
}

void TrayController::rebuildDeviceActions()
{
    for (QAction* a : std::as_const(m_deviceActions)) {
        m_menu->removeAction(a);
        delete a;
    }
    m_deviceActions.clear();
    if (m_context) {
        for (const QUuid& id : m_context->monitor()->deviceIds()) {
            auto* action = new QAction(m_menu.get());
            action->setData(id);
            connect(action, &QAction::triggered, this, &TrayController::openRequested);
            m_menu->insertAction(m_deviceSeparator, action);
            m_deviceActions << action;
        }
    }
    m_deviceSeparator->setVisible(!m_deviceActions.isEmpty());
    update();
}

void TrayController::update()
{
    m_debounce.stop();
    if (!m_context)
        return;
    const Monitor& monitor = *m_context->monitor();

    // The device the icon describes: the worst one; ties go to the lowest charge.
    QUuid worst;
    Monitor::Health worstHealth = Monitor::Health::Ok;
    double worstCharge = 101;
    QStringList lines { QStringLiteral("nutty") };
    for (QAction* action : std::as_const(m_deviceActions)) {
        const QUuid id = action->data().toUuid();
        const QString line = deviceinfo::trayLine(monitor, id);
        action->setText(line);
        lines << line;
        const DeviceConfig* d = monitor.device(id);
        if (!d || !d->enabled)
            continue;
        const Monitor::Health h = monitor.health(id);
        const double charge = monitor.status(id).charge.value_or(100);
        if (worst.isNull() || h > worstHealth || (h == worstHealth && charge < worstCharge)) {
            worst = id;
            worstHealth = h;
            worstCharge = charge;
        }
    }
    if (m_deviceActions.isEmpty())
        lines << tr("No devices yet");

    const auto pending = monitor.pendingAction();
    m_pending->setVisible(pending.has_value());
    m_cancel->setVisible(pending && pending->cancellable && !pending->fsd);
    if (pending) {
        const QString title = ActionController::countdownTitle(enumKey(pending->type).toLower(),
            static_cast<int>(pending->remainingSec(monitor.now())), pending->dryRun || monitor.dryRun());
        m_pending->setText(title);
        lines << title;
    }
    const bool snoozed = monitor.snoozedUntil().isValid() && monitor.snoozedUntil() > monitor.now();
    m_snooze->setText(snoozed ? tr("Resume power actions") : tr("Snooze power actions for 1 hour"));

    TrayIconState state = m_state;
    if (worst.isNull()) {
        state.health = QStringLiteral("unknown");
        state.charge = -1;
        state.charging = false;
    } else {
        const DeviceStatus status = monitor.status(worst);
        state.health = deviceinfo::healthKey(worstHealth);
        state.charge = status.charge ? static_cast<int>(std::lround(*status.charge)) : -1;
        state.charging = status.has(StatusFlag::Charging) && !status.onBattery();
    }
    state.pending = pending.has_value();

    m_icon->setToolTip(lines.join(u'\n'));
    if (!m_iconSet || !(state == m_state)) {
        m_state = state;
        m_iconSet = true;
        QIcon icon(new TrayIconEngine(state));
        icon.setIsMask(state.mask);
        m_icon->setIcon(icon);
    }
}

}  // namespace nutty
