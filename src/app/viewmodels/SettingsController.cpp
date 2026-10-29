// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "app/viewmodels/SettingsController.h"

#include "app/viewmodels/AppContext.h"
#include "core/model/SecretStore.h"
#include "core/storage/ConfigStore.h"
#include "platform/autostart/Autostart.h"

#include <QDir>
#include <QLocale>
#include <QRegularExpression>

#include <algorithm>

namespace nutty {

SettingsController::SettingsController(AppContext* context, QObject* parent)
    : QObject(parent)
    , m_context(context ? context : AppContext::instance())
{
    revert();
    if (m_context) {
        connect(m_context, &AppContext::configChanged, this, [this] {
            if (!m_saving)
                revert();
        });
        connect(m_context, &AppContext::lastErrorChanged, this, &SettingsController::lastErrorChanged);
    }
}

SettingsController* SettingsController::create(QQmlEngine*, QJSEngine*)
{
    return new SettingsController(AppContext::instance());
}

template <typename T, typename V> void SettingsController::assign(T& field, const V& value)
{
    if (field == value)
        return;
    field = value;
    emit changed();
    save();
}

QString SettingsController::theme() const
{
    return enumKey(m_general.theme);
}

void SettingsController::setTheme(const QString& theme)
{
    assign(m_general.theme, enumFromKey(theme, ThemeMode::System));
}

QString SettingsController::motion() const
{
    return enumKey(m_general.motion);
}

void SettingsController::setMotion(const QString& motion)
{
    assign(m_general.motion, enumFromKey(motion, MotionMode::System));
}

void SettingsController::setTranslucentWindow(bool on)
{
    assign(m_general.translucentWindow, on);
}

void SettingsController::setUseSystemTitleBar(bool on)
{
    assign(m_general.useSystemTitleBar, on);
}

void SettingsController::setAutostart(bool on)
{
    assign(m_general.autostart, on);
}

void SettingsController::setKeepWindowLoaded(bool on)
{
    assign(m_general.keepWindowLoaded, on);
}

QString SettingsController::trayIcon() const
{
    return enumKey(m_general.trayIcon);
}

void SettingsController::setTrayIcon(const QString& style)
{
    assign(m_general.trayIcon, enumFromKey(style, TrayIconStyle::Acorn));
}

void SettingsController::setMonochromeTrayIcon(bool on)
{
    assign(m_general.monochromeTrayIcon, on);
}

void SettingsController::setLanguage(const QString& language)
{
    assign(m_general.language, language);
}

void SettingsController::setSampleRetentionDays(int days)
{
    assign(m_general.sampleRetentionDays, std::clamp(days, 0, 3650));
}

void SettingsController::setEventRetentionDays(int days)
{
    assign(m_general.eventRetentionDays, std::clamp(days, 0, 3650));
}

void SettingsController::setSafetyMarginSec(int s)
{
    assign(m_actions.safetyMarginSec, std::clamp(s, 0, 3600));
}

void SettingsController::setStableOnlineSec(int s)
{
    assign(m_actions.stableOnlineSec, std::clamp(s, 0, 3600));
}

void SettingsController::setDeadTimeSec(int s)
{
    assign(m_actions.deadTimeSec, std::clamp(s, 5, 3600));
}

void SettingsController::setWakeGraceSec(int s)
{
    assign(m_actions.wakeGraceSec, std::clamp(s, 0, 600));
}

void SettingsController::setFsdDelaySec(int s)
{
    assign(m_actions.fsdDelaySec, std::clamp(s, 0, 120));
}

QStringList SettingsController::availableLanguageCodes()
{
    QStringList codes;
    static const QRegularExpression pattern(QStringLiteral("^nutty_(.+)\\.qm$"));
    const auto entries
        = QDir(QStringLiteral(":/i18n")).entryList({ QStringLiteral("nutty_*.qm") }, QDir::Files);
    for (const QString& file : entries) {
        if (const auto m = pattern.match(file); m.hasMatch())
            codes << m.captured(1);
    }
    codes.sort();
    return codes;
}

QVariantList SettingsController::languages() const
{
    QVariantList list;
    list.append(QVariantMap {
        { QStringLiteral("code"), QString() }, { QStringLiteral("name"), tr("System default") } });
    QStringList codes = availableLanguageCodes();
    if (!codes.contains(u"en"))
        codes.prepend(QStringLiteral("en"));
    QList<QPair<QString, QString>> named;
    for (const QString& code : std::as_const(codes)) {
        const QLocale locale(code);
        QString name = locale.nativeLanguageName();
        if (code.contains(u'_'))
            name += QStringLiteral(" (%1)").arg(locale.nativeTerritoryName());
        if (!name.isEmpty())
            name[0] = name[0].toUpper();
        named.append({ code, name.isEmpty() ? code : name });
    }
    std::ranges::sort(
        named, [](const auto& a, const auto& b) { return a.second.localeAwareCompare(b.second) < 0; });
    for (const auto& [code, name] : named)
        list.append(QVariantMap { { QStringLiteral("code"), code }, { QStringLiteral("name"), name } });
    return list;
}

bool SettingsController::secureStorage() const
{
    SecretStore* store = m_context ? m_context->secrets() : nullptr;
    return store && store->isSecure();
}

QString SettingsController::secretBackend() const
{
    SecretStore* store = m_context ? m_context->secrets() : nullptr;
    return store ? store->backendName() : tr("Memory only");
}

QString SettingsController::secretWarning() const
{
    if (secureStorage())
        return {};
    SecretStore* store = m_context ? m_context->secrets() : nullptr;
    if (!store)
        return tr("Passwords and tokens are kept in memory only and must be entered again after a restart.");
    return tr("No system keychain was found, so passwords and tokens are stored in a file only you can read. "
              "It is obfuscated, not encrypted. Install a keyring (such as GNOME Keyring or KWallet) to keep "
              "them safer.");
}

bool SettingsController::autostartSupported() const
{
    return Autostart::isSupported();
}

bool SettingsController::titleBarOption() const
{
#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)
    return true;
#else
    return false;
#endif
}

bool SettingsController::translucencyOption() const
{
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    return true;
#else
    return false;
#endif
}

QString SettingsController::configPath() const
{
    ConfigStore* store = m_context ? m_context->configStore() : nullptr;
    return store ? displayPath(store->path()) : QString();
}

QString SettingsController::lastError() const
{
    return m_context ? m_context->lastError() : QString();
}

bool SettingsController::save()
{
    if (!m_context)
        return false;
    AppConfig config = m_context->config();
    config.general = m_general;
    config.actions = m_actions;
    m_saving = true;
    const bool ok = m_context->updateConfig(config);
    m_saving = false;
    return ok;
}

void SettingsController::revert()
{
    if (!m_context)
        return;
    m_general = m_context->config().general;
    m_actions = m_context->config().actions;
    emit changed();
}

}  // namespace nutty
