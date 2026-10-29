// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/appearance/SystemAppearance.h"

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QTimer>

#include <functional>
#include <windows.h>

// Reads the personalization settings from the registry. Changes arrive as
// WM_SETTINGCHANGE on top-level windows; a slow poll covers a tray-only app
// that has no window to receive them.

namespace nutty {

namespace {

constexpr int kPollIntervalMs = 5000;

std::optional<DWORD> readUserDword(const wchar_t* subKey, const wchar_t* value)
{
    DWORD data = 0;
    DWORD size = sizeof(data);
    if (RegGetValueW(HKEY_CURRENT_USER, subKey, value, RRF_RT_REG_DWORD, nullptr, &data, &size)
        != ERROR_SUCCESS)
        return std::nullopt;
    return data;
}

std::optional<bool> readDark()
{
    const auto light = readUserDword(
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme");
    if (!light)
        return std::nullopt;
    return *light == 0;
}

QColor readAccentColor()
{
    // 0xAABBGGRR
    const auto accent = readUserDword(L"Software\\Microsoft\\Windows\\DWM", L"AccentColor");
    if (!accent)
        return {};
    return QColor(int(*accent & 0xff), int((*accent >> 8) & 0xff), int((*accent >> 16) & 0xff));
}

bool readReduceMotion()
{
    BOOL animations = TRUE;
    if (!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0))
        return false;
    return !animations;
}

class SettingChangeFilter : public QAbstractNativeEventFilter
{
public:
    explicit SettingChangeFilter(std::function<void()> onChange)
        : m_onChange(std::move(onChange))
    {
    }

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr*) override
    {
        if (eventType != "windows_generic_MSG")
            return false;
        const auto* msg = static_cast<const MSG*>(message);
        if (msg->message == WM_SETTINGCHANGE || msg->message == WM_THEMECHANGED
            || msg->message == WM_DWMCOLORIZATIONCOLORCHANGED)
            m_onChange();
        return false;
    }

private:
    std::function<void()> m_onChange;
};

}  // namespace

struct SystemAppearance::Private {
    std::unique_ptr<SettingChangeFilter> filter;
    QTimer* poll = nullptr;
};

SystemAppearance::SystemAppearance(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    watchQtColorScheme();

    const auto refresh = [this] {
        updateDark(readDark());
        updateAccentColor(readAccentColor());
        updateReduceMotion(readReduceMotion());
    };
    refresh();

    // Several messages arrive per change; coalesce them into one refresh.
    auto* coalesce = new QTimer(this);
    coalesce->setSingleShot(true);
    coalesce->setInterval(100);
    connect(coalesce, &QTimer::timeout, this, refresh);
    d->filter = std::make_unique<SettingChangeFilter>([coalesce] { coalesce->start(); });
    if (auto* app = QCoreApplication::instance())
        app->installNativeEventFilter(d->filter.get());

    d->poll = new QTimer(this);
    d->poll->setInterval(kPollIntervalMs);
    connect(d->poll, &QTimer::timeout, this, refresh);
    d->poll->start();
}

SystemAppearance::~SystemAppearance()
{
    if (auto* app = QCoreApplication::instance())
        app->removeNativeEventFilter(d->filter.get());
}

}  // namespace nutty
