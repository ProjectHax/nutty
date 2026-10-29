// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Types.h"

#include <QColor>
#include <QFont>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

namespace nutty {

// The design tokens every QML file uses, exposed as the singleton `Theme`
// (import Nutty). It turns a few inputs into a full palette:
//
//   - mode (System/Light/Dark) and motionMode (System/On/Off), from settings;
//   - the system's dark flag, accent colour and reduce-motion preference.
//     Until SystemAppearance (platform) feeds them through the setters, they
//     come from QStyleHints / QPalette;
//   - the platform (windows/macos/gnome/kde/linux), which picks radii and
//     title-bar button styles.
//
// It also pushes a matching QPalette to the application on Linux (Fusion) and,
// on Qt >= 6.8, the forced colour scheme to the native styles.
class ThemeBridge : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Theme)
    QML_SINGLETON

    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    Q_PROPERTY(QString mode READ modeKey WRITE setModeKey NOTIFY changed)
    Q_PROPERTY(QString motionMode READ motionModeKey WRITE setMotionModeKey NOTIFY changed)
    Q_PROPERTY(QString platform READ platform CONSTANT)
    Q_PROPERTY(bool effectsEnabled READ effectsEnabled NOTIFY changed)
    Q_PROPERTY(bool reduceMotion READ reduceMotion NOTIFY changed)
    Q_PROPERTY(bool animationsEnabled READ animationsEnabled NOTIFY changed)
    Q_PROPERTY(int durationFast READ durationFast NOTIFY changed)
    Q_PROPERTY(int durationNormal READ durationNormal NOTIFY changed)
    Q_PROPERTY(int durationSlow READ durationSlow NOTIFY changed)

    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor accentText READ accentText NOTIFY changed)
    Q_PROPERTY(QColor window READ window NOTIFY changed)
    Q_PROPERTY(QColor surface READ surface NOTIFY changed)
    Q_PROPERTY(QColor card READ card NOTIFY changed)
    Q_PROPERTY(QColor cardHover READ cardHover NOTIFY changed)
    Q_PROPERTY(QColor elevated READ elevated NOTIFY changed)
    Q_PROPERTY(QColor text READ text NOTIFY changed)
    Q_PROPERTY(QColor subtext READ subtext NOTIFY changed)
    Q_PROPERTY(QColor faint READ faint NOTIFY changed)
    Q_PROPERTY(QColor border READ border NOTIFY changed)
    Q_PROPERTY(QColor divider READ divider NOTIFY changed)
    Q_PROPERTY(QColor hover READ hover NOTIFY changed)
    Q_PROPERTY(QColor pressed READ pressed NOTIFY changed)
    Q_PROPERTY(QColor shadow READ shadow NOTIFY changed)
    Q_PROPERTY(QColor success READ success NOTIFY changed)
    Q_PROPERTY(QColor warning READ warning NOTIFY changed)
    Q_PROPERTY(QColor critical READ critical NOTIFY changed)
    Q_PROPERTY(QColor info READ info NOTIFY changed)

    Q_PROPERTY(int radius READ radius CONSTANT)
    Q_PROPERTY(int radiusSmall READ radiusSmall CONSTANT)
    Q_PROPERTY(int radiusLarge READ radiusLarge CONSTANT)
    Q_PROPERTY(int radiusWindow READ radiusWindow CONSTANT)
    Q_PROPERTY(int spacing READ spacing CONSTANT)
    Q_PROPERTY(int padding READ padding CONSTANT)
    // Buttons, fields, dropdowns and segmented controls, so they line up
    // side by side.
    Q_PROPERTY(int controlHeight READ controlHeight CONSTANT)
    // List cards (settings, channels, rules, groups, devices): the card's
    // padding, how far rows indent their content and dividers inside it, and
    // the least a row is tall, whatever control it holds.
    Q_PROPERTY(int listPadding READ listPadding CONSTANT)
    Q_PROPERTY(int rowInset READ rowInset CONSTANT)
    Q_PROPERTY(int rowHeight READ rowHeight CONSTANT)

    Q_PROPERTY(QFont font READ font NOTIFY changed)
    Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY changed)
    Q_PROPERTY(int fontSize READ fontSize NOTIFY changed)
    Q_PROPERTY(int fontSizeSmall READ fontSizeSmall NOTIFY changed)
    Q_PROPERTY(int fontSizeLarge READ fontSizeLarge NOTIFY changed)
    Q_PROPERTY(int fontSizeTitle READ fontSizeTitle NOTIFY changed)
    Q_PROPERTY(int fontSizeDisplay READ fontSizeDisplay NOTIFY changed)

    // Linux custom title bar.
    Q_PROPERTY(bool frameless READ frameless WRITE setFrameless NOTIFY changed)
    Q_PROPERTY(QString buttonLayout READ buttonLayout WRITE setButtonLayout NOTIFY changed)

public:
    // Not default-constructible on purpose: QML would otherwise make its own
    // instance instead of calling create().
    explicit ThemeBridge(QObject* parent);
    ~ThemeBridge() override;

    // The process-wide instance; QML's `Theme` is this object.
    static ThemeBridge* instance();
    static ThemeBridge* create(QQmlEngine*, QJSEngine*);

    bool dark() const;
    ThemeMode mode() const { return m_mode; }
    void setMode(ThemeMode mode);
    QString modeKey() const;
    void setModeKey(const QString& key);
    MotionMode motionMode() const { return m_motion; }
    void setMotionMode(MotionMode mode);
    QString motionModeKey() const;
    void setMotionModeKey(const QString& key);
    QString platform() const { return m_platform; }
    bool effectsEnabled() const { return m_effectsEnabled; }
    void setEffectsEnabled(bool enabled);
    bool reduceMotion() const;
    bool animationsEnabled() const { return !reduceMotion(); }
    int durationFast() const { return reduceMotion() ? 0 : 120; }
    int durationNormal() const { return reduceMotion() ? 0 : 220; }
    int durationSlow() const { return reduceMotion() ? 0 : 450; }

    // Inputs from the OS (SystemAppearance calls these).
    void setSystemDark(bool dark);
    void setSystemAccent(const QColor& accent);
    void setSystemReduceMotion(bool reduce);
    bool systemDark() const { return m_systemDark; }

    QColor accent() const;
    QColor accentText() const;
    QColor window() const;
    QColor surface() const;
    QColor card() const;
    QColor cardHover() const;
    QColor elevated() const;
    QColor text() const;
    QColor subtext() const;
    QColor faint() const;
    QColor border() const;
    QColor divider() const;
    QColor hover() const;
    QColor pressed() const;
    QColor shadow() const;
    QColor success() const;
    QColor warning() const;
    QColor critical() const;
    QColor info() const { return accent(); }

    int radius() const { return m_radius; }
    int radiusSmall() const { return qMax(3, m_radius - 2); }
    int radiusLarge() const { return m_radius + 2; }
    int radiusWindow() const { return m_windowRadius; }
    int spacing() const { return 8; }
    int padding() const { return 16; }
    int controlHeight() const { return 36; }
    int listPadding() const { return 8; }
    int rowInset() const { return 12; }
    int rowHeight() const { return 64; }

    QFont font() const { return m_font; }
    QString fontFamily() const { return m_font.family(); }
    int fontSize() const { return m_fontPx; }
    int fontSizeSmall() const { return qMax(10, m_fontPx - 2); }
    int fontSizeLarge() const { return m_fontPx + 2; }
    int fontSizeTitle() const { return m_fontPx + 6; }
    int fontSizeDisplay() const { return m_fontPx * 2 + 4; }

    bool frameless() const { return m_frameless; }
    void setFrameless(bool frameless);
    QString buttonLayout() const { return m_buttonLayout; }
    void setButtonLayout(const QString& layout);

    // Colour for a battery level: green above 50 %, amber to 20 %, red below.
    Q_INVOKABLE QColor levelColor(qreal charge) const;
    // Colour for a device state key (see StatusChip.qml for the list).
    Q_INVOKABLE QColor stateColor(const QString& state) const;
    // `color` with its alpha replaced; Qt.alpha() only exists from Qt 6.5.
    Q_INVOKABLE QColor alpha(const QColor& color, qreal alpha) const;
    // Linear blend from a to b.
    Q_INVOKABLE QColor mix(const QColor& a, const QColor& b, qreal t) const;

signals:
    void changed();

private:
    void applyToApplication();
    void detectFromQt();

    ThemeMode m_mode = ThemeMode::System;
    MotionMode m_motion = MotionMode::System;
    bool m_systemDark = false;
    bool m_systemReduceMotion = false;
    bool m_effectsEnabled = true;
    bool m_frameless = false;
    QColor m_systemAccent;
    QString m_platform;
    QString m_buttonLayout;
    QFont m_font;
    int m_fontPx = 14;
    int m_radius = 6;
    int m_windowRadius = 10;
};

}  // namespace nutty
