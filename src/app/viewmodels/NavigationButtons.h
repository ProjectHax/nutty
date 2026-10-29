// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include <QObject>
#include <QPointer>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

namespace nutty {

// The mouse's back and forward buttons. They are caught on the window
// itself, before Qt Quick hands the press to any item, so nothing on a page
// can swallow them. back() and forward() fire on the press; the press and
// the release stop here either way.
//
//   NavigationButtons { window: win; onBack: win.goBack() }
class NavigationButtons : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QWindow* window READ window WRITE setWindow NOTIFY windowChanged)
    // While false the buttons do nothing, as when a dialog is open.
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)

public:
    explicit NavigationButtons(QObject* parent = nullptr);

    QWindow* window() const { return m_window; }
    void setWindow(QWindow* window);
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);

signals:
    void windowChanged();
    void enabledChanged();
    void back();
    void forward();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QPointer<QWindow> m_window;
    bool m_enabled = true;
};

}  // namespace nutty
