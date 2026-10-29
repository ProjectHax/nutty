// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "app/viewmodels/ThemeBridge.h"

#include <QFontDatabase>
#include <QFontInfo>
#include <QGuiApplication>
#include <QJSEngine>
#include <QPalette>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QStyleHints>

#include <algorithm>

namespace nutty {

namespace {

ThemeBridge* g_instance = nullptr;

QString detectPlatform()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower();
    if (desktop.contains(u"kde"))
        return QStringLiteral("kde");
    if (desktop.contains(u"gnome") || desktop.contains(u"unity") || desktop.contains(u"pantheon")
        || desktop.contains(u"budgie") || desktop.contains(u"cinnamon"))
        return QStringLiteral("gnome");
    return QStringLiteral("linux");
#endif
}

// Qt's Fusion default highlight; when the palette still has it, no platform
// theme supplied a real accent colour.
bool isFusionDefaultHighlight(const QColor& c)
{
    return c == QColor(0x30, 0x8c, 0xc6) || c == QColor(0x2a, 0x82, 0xda);
}

QColor defaultAccent(const QString& platform)
{
    if (platform == u"gnome")
        return QColor(0x35, 0x84, 0xe4);  // libadwaita blue
    if (platform == u"kde")
        return QColor(0x3d, 0xae, 0xe9);  // Breeze highlight
    if (platform == u"macos")
        return QColor(0x0a, 0x84, 0xff);
    return QColor(0x00, 0x67, 0xc0);  // Windows 11 default
}

QColor withAlpha(QColor c, qreal a)
{
    c.setAlphaF(static_cast<float>(std::clamp(a, 0.0, 1.0)));
    return c;
}

QColor blend(const QColor& a, const QColor& b, qreal t)
{
    t = std::clamp(t, 0.0, 1.0);
    const auto lerp = [t](float x, float y) { return x + (y - x) * static_cast<float>(t); };
    QColor out;
    out.setRgbF(lerp(a.redF(), b.redF()), lerp(a.greenF(), b.greenF()), lerp(a.blueF(), b.blueF()),
        lerp(a.alphaF(), b.alphaF()));
    return out;
}

}  // namespace

ThemeBridge::ThemeBridge(QObject* parent)
    : QObject(parent)
    , m_platform(detectPlatform())
{
    if (!g_instance)
        g_instance = this;

    // Controls and cards stay close to square; the window keeps the
    // desktop's own corners.
    if (m_platform == u"windows") {
        m_radius = 4;
        m_windowRadius = 8;
    } else if (m_platform == u"macos") {
        m_radius = 6;
        m_windowRadius = 10;
    } else if (m_platform == u"gnome") {
        m_radius = 6;
        m_windowRadius = 12;
        m_buttonLayout = QStringLiteral("appmenu:minimize,maximize,close");
    } else if (m_platform == u"kde") {
        m_radius = 4;
        m_windowRadius = 6;
        m_buttonLayout = QStringLiteral("appmenu:minimize,maximize,close");
    } else {
        m_buttonLayout = QStringLiteral("appmenu:minimize,maximize,close");
    }

    m_font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    if (qGuiApp) {
        m_fontPx = std::clamp(QFontInfo(m_font).pixelSize(), 12, 20);
    }

    m_effectsEnabled = QQuickWindow::graphicsApi() != QSGRendererInterface::Software
        && !qEnvironmentVariableIsSet("NUTTY_NO_EFFECTS");

#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)
    m_frameless = true;
#endif

    detectFromQt();
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (qGuiApp) {
        connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
            const auto scheme = QGuiApplication::styleHints()->colorScheme();
            if (scheme != Qt::ColorScheme::Unknown)
                setSystemDark(scheme == Qt::ColorScheme::Dark);
        });
    }
#endif
    applyToApplication();
}

ThemeBridge::~ThemeBridge()
{
    if (g_instance == this)
        g_instance = nullptr;
}

ThemeBridge* ThemeBridge::instance()
{
    if (!g_instance)
        g_instance = new ThemeBridge(qGuiApp);
    return g_instance;
}

ThemeBridge* ThemeBridge::create(QQmlEngine*, QJSEngine*)
{
    auto* theme = instance();
    QJSEngine::setObjectOwnership(theme, QJSEngine::CppOwnership);
    return theme;
}

void ThemeBridge::detectFromQt()
{
    if (!qGuiApp)
        return;
    const QPalette palette = QGuiApplication::palette();
    bool dark = palette.color(QPalette::Window).lightness() < 128;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    const auto scheme = QGuiApplication::styleHints()->colorScheme();
    if (scheme != Qt::ColorScheme::Unknown)
        dark = scheme == Qt::ColorScheme::Dark;
#endif
    m_systemDark = dark;
    const QColor highlight = palette.color(QPalette::Active, QPalette::Highlight);
    if (!m_systemAccent.isValid() && !isFusionDefaultHighlight(highlight) && m_platform != u"gnome")
        m_systemAccent = highlight;
}

bool ThemeBridge::dark() const
{
    switch (m_mode) {
    case ThemeMode::Light:
        return false;
    case ThemeMode::Dark:
        return true;
    case ThemeMode::System:
        break;
    }
    return m_systemDark;
}

void ThemeBridge::setMode(ThemeMode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    applyToApplication();
    emit changed();
}

QString ThemeBridge::modeKey() const
{
    return enumKey(m_mode);
}
void ThemeBridge::setModeKey(const QString& key)
{
    setMode(enumFromKey(key, ThemeMode::System));
}

void ThemeBridge::setMotionMode(MotionMode mode)
{
    if (m_motion == mode)
        return;
    m_motion = mode;
    emit changed();
}

QString ThemeBridge::motionModeKey() const
{
    return enumKey(m_motion);
}
void ThemeBridge::setMotionModeKey(const QString& key)
{
    setMotionMode(enumFromKey(key, MotionMode::System));
}

void ThemeBridge::setEffectsEnabled(bool enabled)
{
    if (m_effectsEnabled == enabled)
        return;
    m_effectsEnabled = enabled;
    emit changed();
}

bool ThemeBridge::reduceMotion() const
{
    switch (m_motion) {
    case MotionMode::On:
        return false;
    case MotionMode::Off:
        return true;
    case MotionMode::System:
        break;
    }
    return m_systemReduceMotion;
}

void ThemeBridge::setSystemDark(bool dark)
{
    if (m_systemDark == dark)
        return;
    m_systemDark = dark;
    applyToApplication();
    emit changed();
}

void ThemeBridge::setSystemAccent(const QColor& accent)
{
    if (m_systemAccent == accent)
        return;
    m_systemAccent = accent;
    applyToApplication();
    emit changed();
}

void ThemeBridge::setSystemReduceMotion(bool reduce)
{
    if (m_systemReduceMotion == reduce)
        return;
    m_systemReduceMotion = reduce;
    emit changed();
}

void ThemeBridge::setFrameless(bool frameless)
{
    if (m_frameless == frameless)
        return;
    m_frameless = frameless;
    emit changed();
}

void ThemeBridge::setButtonLayout(const QString& layout)
{
    if (m_buttonLayout == layout)
        return;
    m_buttonLayout = layout;
    emit changed();
}

// --- tokens -------------------------------------------------------------------

QColor ThemeBridge::accent() const
{
    QColor base = m_systemAccent.isValid() ? m_systemAccent : defaultAccent(m_platform);
    // Keep the accent readable: lift dark accents in dark mode, deepen pale ones in light mode.
    if (dark() && base.lightness() < 110)
        base = base.lighter(135);
    else if (!dark() && base.lightness() > 190)
        base = base.darker(140);
    return base;
}

QColor ThemeBridge::accentText() const
{
    const QColor a = accent();
    const qreal luminance = 0.2126 * a.redF() + 0.7152 * a.greenF() + 0.0722 * a.blueF();
    return luminance > 0.6 ? QColor(0x10, 0x12, 0x14) : QColor(Qt::white);
}

QColor ThemeBridge::window() const
{
    return dark() ? QColor(0x16, 0x18, 0x1c) : QColor(0xf4, 0xf5, 0xf7);
}
QColor ThemeBridge::surface() const
{
    return dark() ? QColor(0x1c, 0x1f, 0x24) : QColor(0xea, 0xec, 0xf0);
}
QColor ThemeBridge::card() const
{
    return dark() ? QColor(0x23, 0x27, 0x2d) : QColor(0xff, 0xff, 0xff);
}
QColor ThemeBridge::cardHover() const
{
    return dark() ? QColor(0x28, 0x2d, 0x34) : QColor(0xfb, 0xfc, 0xfd);
}
QColor ThemeBridge::elevated() const
{
    return dark() ? QColor(0x2b, 0x30, 0x37) : QColor(0xff, 0xff, 0xff);
}
QColor ThemeBridge::text() const
{
    return dark() ? QColor(0xec, 0xee, 0xf1) : QColor(0x1a, 0x1d, 0x22);
}
QColor ThemeBridge::subtext() const
{
    return dark() ? QColor(0xa1, 0xa9, 0xb3) : QColor(0x5a, 0x63, 0x6e);
}
QColor ThemeBridge::faint() const
{
    return dark() ? QColor(0x6c, 0x74, 0x7e) : QColor(0x95, 0x9c, 0xa6);
}
QColor ThemeBridge::border() const
{
    return dark() ? withAlpha(Qt::white, 0.08) : withAlpha(Qt::black, 0.08);
}
QColor ThemeBridge::divider() const
{
    return dark() ? withAlpha(Qt::white, 0.06) : withAlpha(Qt::black, 0.06);
}
QColor ThemeBridge::hover() const
{
    return dark() ? withAlpha(Qt::white, 0.06) : withAlpha(Qt::black, 0.045);
}
QColor ThemeBridge::pressed() const
{
    return dark() ? withAlpha(Qt::white, 0.10) : withAlpha(Qt::black, 0.08);
}
QColor ThemeBridge::shadow() const
{
    return dark() ? withAlpha(Qt::black, 0.55) : withAlpha(QColor(0x10, 0x18, 0x28), 0.16);
}
QColor ThemeBridge::success() const
{
    return dark() ? QColor(0x3d, 0xd6, 0x8c) : QColor(0x1a, 0x9f, 0x5c);
}
QColor ThemeBridge::warning() const
{
    return dark() ? QColor(0xf5, 0xb7, 0x3b) : QColor(0xd9, 0x8a, 0x05);
}
QColor ThemeBridge::critical() const
{
    return dark() ? QColor(0xff, 0x5f, 0x63) : QColor(0xdc, 0x35, 0x3a);
}

QColor ThemeBridge::levelColor(qreal charge) const
{
    if (charge < 0)
        return faint();
    if (charge >= 50)
        return blend(warning(), success(), std::min(1.0, (charge - 40) / 20.0));
    if (charge >= 20)
        return blend(critical(), warning(), (charge - 15) / 15.0);
    return critical();
}

QColor ThemeBridge::stateColor(const QString& state) const
{
    if (state == u"online" || state == u"charging" || state == u"ok")
        return success();
    if (state == u"onBattery" || state == u"bypass" || state == u"overload" || state == u"replaceBattery"
        || state == u"warning")
        return warning();
    if (state == u"lowBattery" || state == u"fsd" || state == u"error" || state == u"critical")
        return critical();
    if (state == u"offline")
        return subtext();
    return faint();
}

QColor ThemeBridge::alpha(const QColor& color, qreal a) const
{
    return withAlpha(color, a);
}
QColor ThemeBridge::mix(const QColor& a, const QColor& b, qreal t) const
{
    return blend(a, b, t);
}

void ThemeBridge::applyToApplication()
{
    if (!qGuiApp)
        return;

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    // Native styles (windows11, macOS) follow the requested scheme.
    switch (m_mode) {
    case ThemeMode::System:
        QGuiApplication::styleHints()->unsetColorScheme();
        break;
    case ThemeMode::Light:
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
        break;
    case ThemeMode::Dark:
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
        break;
    }
#endif

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    // Fusion on Linux takes nutty's palette, except on KDE in System mode where
    // the platform theme's own (Breeze) palette is already right.
    if (m_platform == u"kde" && m_mode == ThemeMode::System)
        return;
    QPalette p;
    const QColor base = card();
    const QColor windowColor = window();
    const QColor textColor = text();
    p.setColor(QPalette::Window, windowColor);
    p.setColor(QPalette::WindowText, textColor);
    p.setColor(QPalette::Base, dark() ? QColor(0x1e, 0x21, 0x26) : base);
    p.setColor(QPalette::AlternateBase, surface());
    p.setColor(QPalette::Text, textColor);
    p.setColor(QPalette::Button, dark() ? QColor(0x2d, 0x32, 0x39) : QColor(0xf0, 0xf1, 0xf3));
    p.setColor(QPalette::ButtonText, textColor);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, accent());
    p.setColor(QPalette::HighlightedText, accentText());
    p.setColor(QPalette::ToolTipBase, elevated());
    p.setColor(QPalette::ToolTipText, textColor);
    p.setColor(QPalette::PlaceholderText, faint());
    p.setColor(QPalette::Link, accent());
    p.setColor(QPalette::Light, dark() ? QColor(0x3a, 0x40, 0x48) : Qt::white);
    p.setColor(QPalette::Midlight, dark() ? QColor(0x33, 0x38, 0x40) : QColor(0xe3, 0xe5, 0xe8));
    p.setColor(QPalette::Mid, dark() ? QColor(0x2a, 0x2e, 0x35) : QColor(0xc8, 0xcc, 0xd2));
    p.setColor(QPalette::Dark, dark() ? QColor(0x12, 0x14, 0x17) : QColor(0xa0, 0xa6, 0xae));
    p.setColor(QPalette::Shadow, Qt::black);
    for (const auto role : { QPalette::WindowText, QPalette::Text, QPalette::ButtonText }) {
        p.setColor(QPalette::Disabled, role, faint());
    }
    QGuiApplication::setPalette(p);
#endif
}

}  // namespace nutty
