// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "app/viewmodels/NavigationButtons.h"

#include <QLoggingCategory>
#include <QMouseEvent>

Q_LOGGING_CATEGORY(lcNavigation, "nutty.ui.navigation")

namespace nutty {

NavigationButtons::NavigationButtons(QObject* parent)
    : QObject(parent)
{
}

void NavigationButtons::setWindow(QWindow* window)
{
    if (window == m_window)
        return;
    if (m_window)
        m_window->removeEventFilter(this);
    m_window = window;
    if (m_window)
        m_window->installEventFilter(this);
    emit windowChanged();
}

void NavigationButtons::setEnabled(bool enabled)
{
    if (enabled == m_enabled)
        return;
    m_enabled = enabled;
    emit enabledChanged();
}

bool NavigationButtons::eventFilter(QObject* watched, QEvent* event)
{
    const QEvent::Type type = event->type();
    if (watched != m_window
        || (type != QEvent::MouseButtonPress && type != QEvent::MouseButtonRelease
            && type != QEvent::MouseButtonDblClick))
        return QObject::eventFilter(watched, event);

    const Qt::MouseButton button = static_cast<QMouseEvent*>(event)->button();
    if (button != Qt::BackButton && button != Qt::ForwardButton)
        return QObject::eventFilter(watched, event);

    if (type == QEvent::MouseButtonPress) {
        qCDebug(lcNavigation) << (button == Qt::BackButton ? "back" : "forward") << "button"
                              << (m_enabled ? "" : "(ignored: a dialog is open)");
        if (m_enabled) {
            if (button == Qt::BackButton)
                emit back();
            else
                emit forward();
        }
    }
    return true;
}

}  // namespace nutty
