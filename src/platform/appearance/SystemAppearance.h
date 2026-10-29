// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Types.h"

#include <QColor>
#include <QObject>

#include <memory>
#include <optional>

namespace nutty {

// The desktop's appearance preferences, followed live:
//   Linux:   the settings portal (color-scheme, accent-color, reduced motion /
//            GNOME enable-animations), then Qt's own color scheme
//   Windows: AppsUseLightTheme, the DWM accent color, client-area animations
//   macOS:   AppleInterfaceStyle, controlAccentColor, Reduce Motion
// dark always reports the system preference, even after setAppColorScheme().
class SystemAppearance : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool dark READ isDark NOTIFY darkChanged)
    Q_PROPERTY(QColor accentColor READ accentColor NOTIFY accentColorChanged)
    Q_PROPERTY(bool reduceMotion READ reduceMotion NOTIFY reduceMotionChanged)

public:
    explicit SystemAppearance(QObject* parent = nullptr);
    ~SystemAppearance() override;

    bool isDark() const { return m_dark; }
    // Invalid when the desktop has no accent color.
    QColor accentColor() const { return m_accentColor; }
    bool reduceMotion() const { return m_reduceMotion; }

    // Asks Qt to draw native parts (menus, title bars, dialogs) in this
    // scheme. Needs Qt 6.8; returns false when it can't be applied.
    static bool setAppColorScheme(ThemeMode mode);

signals:
    void darkChanged(bool dark);
    void accentColorChanged(const QColor& color);
    void reduceMotionChanged(bool reduceMotion);

private:
    // Called by the per-OS code. std::nullopt dark means "no opinion", which
    // falls back to Qt's color scheme or the palette.
    void updateDark(std::optional<bool> dark);
    void updateAccentColor(const QColor& color);
    void updateReduceMotion(bool reduceMotion);
    // Follows Qt's color scheme for when the OS code has no opinion.
    void watchQtColorScheme();
    void refreshFallbackDark();
    void setDark(bool dark);

    struct Private;
    std::unique_ptr<Private> d;
    std::optional<bool> m_systemDark;
    bool m_dark = false;
    QColor m_accentColor;
    bool m_reduceMotion = false;
};

}  // namespace nutty
