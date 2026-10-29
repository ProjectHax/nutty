// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/appearance/SystemAppearance.h"

#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>

// Shared half: change tracking and the Qt fallback. The constructors and the
// OS listeners live in SystemAppearance_{linux,win,mac}.

namespace nutty {

namespace {

// Once the app forces a scheme, Qt reports that instead of the system's, so
// the fallback stops following Qt.
bool s_appSchemeForced = false;

}  // namespace

void SystemAppearance::updateDark(std::optional<bool> dark)
{
    m_systemDark = dark;
    if (dark)
        setDark(*dark);
    else
        refreshFallbackDark();
}

void SystemAppearance::updateAccentColor(const QColor& color)
{
    if (color == m_accentColor)
        return;
    m_accentColor = color;
    emit accentColorChanged(m_accentColor);
}

void SystemAppearance::updateReduceMotion(bool reduceMotion)
{
    if (reduceMotion == m_reduceMotion)
        return;
    m_reduceMotion = reduceMotion;
    emit reduceMotionChanged(m_reduceMotion);
}

void SystemAppearance::watchQtColorScheme()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (auto* hints = QGuiApplication::styleHints())
        connect(hints, &QStyleHints::colorSchemeChanged, this, &SystemAppearance::refreshFallbackDark);
#endif
    refreshFallbackDark();
}

void SystemAppearance::refreshFallbackDark()
{
    if (m_systemDark || s_appSchemeForced || !qGuiApp)
        return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    switch (QGuiApplication::styleHints()->colorScheme()) {
    case Qt::ColorScheme::Dark:
        setDark(true);
        return;
    case Qt::ColorScheme::Light:
        setDark(false);
        return;
    case Qt::ColorScheme::Unknown:
        break;
    }
#endif
    // Light text on a dark window background.
    const auto palette = QGuiApplication::palette();
    setDark(palette.color(QPalette::Window).lightness() < palette.color(QPalette::WindowText).lightness());
}

void SystemAppearance::setDark(bool dark)
{
    if (dark == m_dark)
        return;
    m_dark = dark;
    emit darkChanged(m_dark);
}

bool SystemAppearance::setAppColorScheme(ThemeMode mode)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    auto* hints = QGuiApplication::styleHints();
    if (!hints)
        return false;
    switch (mode) {
    case ThemeMode::System:
        s_appSchemeForced = false;
        hints->unsetColorScheme();
        break;
    case ThemeMode::Light:
        s_appSchemeForced = true;
        hints->setColorScheme(Qt::ColorScheme::Light);
        break;
    case ThemeMode::Dark:
        s_appSchemeForced = true;
        hints->setColorScheme(Qt::ColorScheme::Dark);
        break;
    }
    return true;
#else
    Q_UNUSED(mode);
    return false;
#endif
}

}  // namespace nutty
