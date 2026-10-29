// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "app/shell/Application.h"

#include "app/shell/TrayController.h"
#include "app/shell/UiHost.h"
#include "app/viewmodels/AppContext.h"
#include "app/viewmodels/ThemeBridge.h"
#include "core/engine/ActionRunner.h"
#include "core/engine/Monitor.h"
#include "core/notify/Dispatcher.h"
#include "core/notify/Notifiers.h"
#include "core/sources/Sources.h"
#include "core/storage/ConfigStore.h"
#include "core/storage/FileSecretStore.h"
#include "core/storage/HistoryStore.h"
#include "platform/appearance/SystemAppearance.h"
#include "platform/autostart/Autostart.h"
#include "platform/instance/SingleInstance.h"
#include "platform/mac/MacActivation.h"
#include "platform/notify/DesktopNotifier.h"
#include "platform/power/PowerActions.h"
#include "platform/power/SleepWatcher.h"
#include "platform/secrets/SystemSecretStore.h"
#include "platform/system/SystemSources.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLibraryInfo>
#include <QLoggingCategory>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTemporaryDir>
#include <QTranslator>

#ifdef Q_OS_LINUX
#include <QDBusConnection>
#include <QDBusServiceWatcher>
#endif

#include <cstring>

Q_LOGGING_CATEGORY(lcApp, "nutty.app")

namespace nutty {

namespace {

const QStringList kPages { QStringLiteral("home"), QStringLiteral("history"), QStringLiteral("device"),
    QStringLiteral("devices"), QStringLiteral("notifications"), QStringLiteral("actions"),
    QStringLiteral("general"), QStringLiteral("about"), QStringLiteral("countdown") };

bool envFlag(const char* name)
{
    const QByteArray value = qgetenv(name).trimmed().toLower();
    return !value.isEmpty() && value != "0" && value != "false" && value != "no";
}

}  // namespace

bool wantsScreenshot(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "--screenshot") == 0 || std::strcmp(arg, "-screenshot") == 0
            || std::strncmp(arg, "--screenshot=", 13) == 0)
            return true;
    }
    return false;
}

Options parseOptions()
{
    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main", "UPS and battery monitor"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption hidden(QStringLiteral("hidden"),
        QCoreApplication::translate("main", "Start in the system tray without opening the window."));
    const QCommandLineOption dryRun(QStringLiteral("dry-run"),
        QCoreApplication::translate(
            "main", "Simulate power actions instead of carrying them out (also NUTTY_DRY_RUN=1)."));
    const QCommandLineOption configDir(QStringLiteral("config-dir"),
        QCoreApplication::translate("main", "Keep the settings, history and pictures in <dir>."),
        QStringLiteral("dir"));
    const QCommandLineOption screenshot(QStringLiteral("screenshot"),
        QCoreApplication::translate("main", "Render a page to <file> (PNG) once data has loaded, then exit."),
        QStringLiteral("file"));
    const QCommandLineOption page(QStringLiteral("page"),
        QCoreApplication::translate("main", "The page to open: %1.").arg(kPages.join(QStringLiteral(", "))),
        QStringLiteral("name"));
    const QCommandLineOption device(QStringLiteral("device"),
        QCoreApplication::translate("main", "The device for --page device (id or name)."),
        QStringLiteral("device"));
    const QCommandLineOption dark(
        QStringLiteral("dark"), QCoreApplication::translate("main", "Use the dark theme."));
    const QCommandLineOption light(
        QStringLiteral("light"), QCoreApplication::translate("main", "Use the light theme."));
    const QCommandLineOption size(QStringLiteral("size"),
        QCoreApplication::translate("main", "Window size for the screenshot, e.g. 1280x820."),
        QStringLiteral("WxH"));
    const QCommandLineOption delay(QStringLiteral("delay"),
        QCoreApplication::translate("main", "Milliseconds to wait before the screenshot."),
        QStringLiteral("ms"));
    parser.addOptions({ hidden, dryRun, configDir, screenshot, page, device, dark, light, size, delay });
    parser.process(*QCoreApplication::instance());

    Options options;
    options.hidden = parser.isSet(hidden);
    options.dryRun = parser.isSet(dryRun) || envFlag("NUTTY_DRY_RUN");
    options.configDir = parser.value(configDir);
    options.screenshot = parser.value(screenshot);
    options.page = parser.value(page).toLower();
    options.deviceId = parser.value(device);
    if (parser.isSet(dark))
        options.theme = QStringLiteral("dark");
    else if (parser.isSet(light))
        options.theme = QStringLiteral("light");
    if (parser.isSet(delay))
        options.delayMs = parser.value(delay).toInt();
    const QStringList wh = parser.value(size).split(u'x');
    if (wh.size() == 2)
        options.size = QSize(wh[0].toInt(), wh[1].toInt());
    if (!options.page.isEmpty() && !kPages.contains(options.page)) {
        qCritical("Unknown page \"%s\". Pages: %s", qPrintable(options.page), qPrintable(kPages.join(u' ')));
        ::exit(2);
    }
    return options;
}

Application::Application(Options options, QObject* parent)
    : QObject(parent)
    , m_options(std::move(options))
{
}

Application::~Application()
{
    // UI first (it holds view-models on the context), the stores last.
    m_ui.reset();
    m_tray.reset();
    m_context.reset();
    m_dispatcher.reset();
    m_actions.reset();
    m_monitor.reset();
    if (m_history)
        m_history->close();
    m_history.reset();
    qDeleteAll(m_translators);
}

int Application::run()
{
    if (!startServices())
        return m_instance && !m_instance->isPrimary() ? 0 : 1;
    if (!m_options.screenshot.isEmpty()) {
        takeScreenshot();
        return QCoreApplication::exec();
    }
    setupTray();
    return QCoreApplication::exec();
}

bool Application::startServices()
{
    const bool screenshot = !m_options.screenshot.isEmpty();
    if (!m_options.configDir.isEmpty()) {
        m_configDir = QDir(m_options.configDir).absolutePath();
        m_dataDir = m_configDir;
    } else {
        m_configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        m_dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    QDir().mkpath(m_configDir);
    QDir().mkpath(m_dataDir);

    // 1. One nutty per session (per config dir): a second launch shows the first.
    if (!screenshot) {
        QString key = QStringLiteral(NUTTY_APP_ID);
        if (!m_options.configDir.isEmpty())
            key += u'-'
                + QString::fromLatin1(
                    QCryptographicHash::hash(m_configDir.toUtf8(), QCryptographicHash::Sha256)
                        .toHex()
                        .left(12));
        m_instance = std::make_unique<SingleInstance>(key);
        if (!m_instance->isPrimary()) {
            if (!m_options.hidden && !m_instance->sendToPrimary("show"))
                qCWarning(lcApp) << "another nutty is running but did not answer";
            return false;
        }
        connect(m_instance.get(), &SingleInstance::messageReceived, this, &Application::onMessage);
    }

    // 2. Everything the registries can create.
    registerCoreSources();
    registerSystemSource();
    registerCoreNotifiers();
    registerDesktopNotifier([this]() -> QSystemTrayIcon* { return m_tray ? m_tray->icon() : nullptr; });

    // 3. Credentials: the OS keychain, or an owner-only file.
    if (!screenshot)
        m_secrets = createSystemSecretStore();
    if (!m_secrets)
        m_secrets = std::make_unique<FileSecretStore>(m_dataDir + QStringLiteral("/secrets.json"));

    // 4. Config, history, monitor and dispatcher.
    m_configStore
        = std::make_unique<ConfigStore>(m_configDir + QStringLiteral("/nutty.ini"), m_secrets.get());
    m_firstRun = !m_configStore->exists();
    const AppConfig config = m_configStore->load();
    if (!m_configStore->lastError().isEmpty())
        qCWarning(lcApp).noquote() << "config:" << m_configStore->lastError();
    if (m_firstRun && !screenshot)
        m_configStore->save(config);

    // A screenshot reads a copy of the history, so it leaves no trace (the
    // countdown page simulates a rule, which logs an event).
    QString historyPath = m_dataDir + QStringLiteral("/history.db");
    if (screenshot) {
        m_scratch = std::make_unique<QTemporaryDir>();
        const QString copy = m_scratch->filePath(QStringLiteral("history.db"));
        historyPath
            = m_scratch->isValid() && QFile::copy(historyPath, copy) ? copy : QStringLiteral(":memory:");
    }
    m_history = std::make_unique<HistoryStore>();
    if (!m_history->open(historyPath)) {
        qCWarning(lcApp).noquote() << "history:" << m_history->lastError()
                                   << "- keeping history in memory only";
        m_history->open(QStringLiteral(":memory:"));
    }

    m_monitor = std::make_unique<Monitor>(m_history.get());
    m_dispatcher = std::make_unique<Dispatcher>();
    ActionRunner::Backend backend;
    backend.execute = &PowerActions::execute;
    backend.runCommand = &PowerActions::runCommand;
    m_actions = std::make_unique<ActionRunner>(m_monitor.get(), m_history.get(), std::move(backend));
#ifdef Q_OS_MACOS
    // A shutdown through System Events can be stopped by an app that refuses
    // to quit. If we're still here a minute later, sleep is the next best thing.
    m_actions->setShutdownFallback(std::chrono::minutes(1));
#endif
    const bool dryRun = m_options.dryRun || screenshot;
    m_monitor->setDryRun(dryRun);

    AppContext::Services services;
    services.monitor = m_monitor.get();
    services.history = m_history.get();
    services.dispatcher = m_dispatcher.get();
    services.configStore = m_configStore.get();
    services.secrets = m_secrets.get();
    services.dataDir = m_dataDir;
    services.dryRunForced = dryRun;
    m_context = std::make_unique<AppContext>(services);

    // Only the history's eventAdded feeds the log models; the monitor's
    // events go out as notifications. Screenshots never notify.
    if (!screenshot)
        connect(m_monitor.get(), &Monitor::eventRaised, m_dispatcher.get(), &Dispatcher::dispatch);
    connect(m_monitor.get(), &Monitor::pendingActionChanged, this, &Application::onPendingChanged);
    connect(m_dispatcher.get(), &Dispatcher::actionInvoked, this, [this](const QString& action) {
        if (action == u"cancel")
            m_monitor->cancelPendingAction();
        else if (action == u"open")
            m_ui->showMain();
    });
    connect(
        m_dispatcher.get(), &Dispatcher::channelFailed, this, [](const QUuid& channel, const QString& error) {
            qCWarning(lcApp).noquote()
                << "notification channel" << channel.toString(QUuid::WithoutBraces) << "failed:" << error;
        });

    m_ui = std::make_unique<UiHost>();
    connect(m_ui.get(), &UiHost::quitRequested, this, &Application::quit);
    connect(m_ui.get(), &UiHost::windowsChanged, this, &Application::updateActivationPolicy);
    connect(m_ui.get(), &UiHost::mainShown, this, &Application::updateActivationPolicy);
    connect(m_ui.get(), &UiHost::mainHidden, this, &Application::updateActivationPolicy);

    // 5. Sleep/wake and the desktop's appearance.
    if (!screenshot) {
        m_sleep = std::make_unique<SleepWatcher>();
        connect(m_sleep.get(), &SleepWatcher::aboutToSleep, m_monitor.get(), &Monitor::notifySleeping);
        connect(m_sleep.get(), &SleepWatcher::resumed, m_monitor.get(), &Monitor::notifyResumed);
    }
    m_appearance = std::make_unique<SystemAppearance>();
    ThemeBridge* theme = ThemeBridge::instance();
    const auto followDark = [this, theme](bool dark) {
        theme->setSystemDark(dark);
        if (m_tray)
            m_tray->setDarkPanel(dark);
    };
    followDark(m_appearance->isDark());
    if (m_appearance->accentColor().isValid())
        theme->setSystemAccent(m_appearance->accentColor());
    theme->setSystemReduceMotion(m_appearance->reduceMotion());
    connect(m_appearance.get(), &SystemAppearance::darkChanged, this, followDark);
    connect(m_appearance.get(), &SystemAppearance::accentColorChanged, theme, &ThemeBridge::setSystemAccent);
    connect(m_appearance.get(), &SystemAppearance::reduceMotionChanged, theme,
        &ThemeBridge::setSystemReduceMotion);

    // 6 + 7. Settings that act outside the window: autostart, theme,
    // translations (before any QML or tray menu exists), then go.
    applyGeneral(config.general, nullptr);
    connect(m_context.get(), &AppContext::configChanged, this,
        [this](const AppConfig& previous) { applyGeneral(m_context->config().general, &previous.general); });
    m_context->setConfig(config);

    if (!screenshot) {
        // Prune once shortly after startup, then daily.
        m_pruneTimer.setInterval(24 * 3600 * 1000);
        connect(&m_pruneTimer, &QTimer::timeout, this, &Application::prune);
        QTimer::singleShot(60 * 1000, this, [this] {
            prune();
            m_pruneTimer.start();
        });
    }
    qCInfo(lcApp).noquote() << "nutty" << NUTTY_VERSION << (dryRun ? "(dry run)" : "")
                            << "config:" << QDir::toNativeSeparators(m_configStore->path())
                            << "secrets:" << m_secrets->backendName();
    return true;
}

void Application::applyGeneral(const GeneralSettings& general, const GeneralSettings* previous)
{
    ThemeBridge* theme = ThemeBridge::instance();
    ThemeMode mode = general.theme;
    if (m_options.theme == u"dark")
        mode = ThemeMode::Dark;
    else if (m_options.theme == u"light")
        mode = ThemeMode::Light;
    theme->setMode(mode);
    SystemAppearance::setAppColorScheme(mode);
    theme->setMotionMode(general.motion);
#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)
    theme->setFrameless(!general.useSystemTitleBar);
#endif
    m_ui->setTranslucent(general.translucentWindow);
    m_ui->setKeepLoaded(general.keepWindowLoaded);
    if (m_tray) {
        m_tray->setStyle(general.trayIcon);
        m_tray->setMonochrome(general.monochromeTrayIcon);
    }

    if (!previous || previous->language != general.language)
        installTranslators(general.language);

    const bool screenshot = !m_options.screenshot.isEmpty();
    if (!screenshot && Autostart::isSupported()) {
        const bool differs = previous ? previous->autostart != general.autostart
                                      : Autostart::isEnabled() != general.autostart;
        if (differs) {
            if (const auto result = Autostart::setEnabled(general.autostart); !result)
                qCWarning(lcApp).noquote() << "autostart:" << result.error();
        }
    }
}

void Application::installTranslators(const QString& language)
{
    for (QTranslator* t : std::as_const(m_translators)) {
        QCoreApplication::removeTranslator(t);
        delete t;
    }
    m_translators.clear();

    // The layout direction comes from the translation of this string.
    //: Translate as "RTL" for right-to-left languages, otherwise "LTR".
    static const char* const kLayoutDirection = QT_TRANSLATE_NOOP("QGuiApplication", "QT_LAYOUT_DIRECTION");
    Q_UNUSED(kLayoutDirection)

    const QLocale locale = language.isEmpty() ? QLocale::system() : QLocale(language);
    if (!language.isEmpty())
        QLocale::setDefault(locale);
    // Without a catalog for the language (or in the "C" locale), English:
    // its catalog holds the "%n device(s)" plural forms.
    auto* app = new QTranslator(this);
    if (app->load(locale, QStringLiteral("nutty"), QStringLiteral("_"), QStringLiteral(":/i18n"))
        || app->load(QLocale(QLocale::English), QStringLiteral("nutty"), QStringLiteral("_"),
            QStringLiteral(":/i18n"))) {
        QCoreApplication::installTranslator(app);
        m_translators << app;
    } else {
        delete app;
    }
    auto* qt = new QTranslator(this);
    if (qt->load(locale, QStringLiteral("qtbase"), QStringLiteral("_"),
            QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        QCoreApplication::installTranslator(qt);
        m_translators << qt;
    } else {
        delete qt;
    }
    m_language = language;
    if (m_ui)
        m_ui->retranslate();
    if (m_tray)
        m_tray->retranslate();
}

void Application::setupTray()
{
    const bool showWindow = !m_options.hidden || m_firstRun;
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        createTray();
        if (showWindow)
            m_ui->showMain(m_options.page);
        return;
    }

    // No tray (yet): the window is the app, and closing it quits.
    m_ui->setQuitOnClose(true);
#ifdef Q_OS_LINUX
    // The panel's StatusNotifierWatcher may simply not be up yet (autostart
    // races the desktop), so keep listening for it.
    auto* watcher = new QDBusServiceWatcher(QStringLiteral("org.kde.StatusNotifierWatcher"),
        QDBusConnection::sessionBus(), QDBusServiceWatcher::WatchForRegistration, this);
    m_trayWatcher = watcher;
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, [this] {
        // Give the panel a moment to register itself as the host.
        QTimer::singleShot(1000, this, [this] {
            if (m_tray || !QSystemTrayIcon::isSystemTrayAvailable())
                return;
            qCInfo(lcApp) << "a system tray appeared";
            createTray();
            m_ui->setQuitOnClose(false);
            m_trayGrace.stop();
        });
    });
    if (!showWindow) {
        m_trayGrace.setSingleShot(true);
        m_trayGrace.setInterval(5000);
        connect(&m_trayGrace, &QTimer::timeout, this, [this] {
            if (!m_tray)
                m_ui->showMain(m_options.page);
        });
        m_trayGrace.start();
        return;
    }
#endif
    m_ui->showMain(m_options.page);
}

void Application::createTray()
{
    m_tray = std::make_unique<TrayController>(m_context.get());
    m_tray->setStyle(m_context->config().general.trayIcon);
    m_tray->setMonochrome(m_context->config().general.monochromeTrayIcon);
    m_tray->setDarkPanel(m_appearance ? m_appearance->isDark() : true);
    connect(m_tray.get(), &TrayController::openRequested, this, [this] { m_ui->showMain(); });
    connect(m_tray.get(), &TrayController::toggleRequested, this, [this] { m_ui->toggleMain(); });
    connect(m_tray.get(), &TrayController::quitRequested, this, &Application::quit);
    updateActivationPolicy();
}

void Application::onMessage(const QByteArray& message)
{
    if (message == "show" || message.startsWith("show:"))
        m_ui->showMain(QString::fromUtf8(message.mid(5)));
}

void Application::onPendingChanged()
{
    const auto pending = m_monitor->pendingAction();
    if (!pending) {
        m_countdownKey.clear();
        m_ui->closeCountdown();
        return;
    }
    if (!m_options.screenshot.isEmpty())
        return;
    // Once per pending action: closing the alert hides it for good.
    const QString key = pending->ruleId.toString() + pending->scheduledAt.toString(Qt::ISODateWithMs);
    if (key == m_countdownKey)
        return;
    m_countdownKey = key;
    m_ui->showCountdown();
}

void Application::updateActivationPolicy()
{
    const bool regular = (m_ui && (m_ui->mainVisible() || m_ui->countdownWindow())) || !m_tray;
    MacActivation::setRegularActivationPolicy(regular);
}

void Application::prune()
{
    const GeneralSettings& g = m_context->config().general;
    m_history->prune(g.sampleRetentionDays, g.eventRetentionDays);
}

void Application::takeScreenshot()
{
    // Wait until every enabled device has answered (data or an error), at
    // most a few seconds, so the page shows real numbers.
    auto* poll = new QTimer(this);
    poll->setInterval(100);
    const qint64 started = QDateTime::currentMSecsSinceEpoch();
    connect(poll, &QTimer::timeout, this, [this, poll, started] {
        bool ready = true;
        for (const QUuid& id : m_monitor->deviceIds()) {
            const DeviceConfig* d = m_monitor->device(id);
            if (!d || !d->enabled)
                continue;
            if (!m_monitor->status(id).isValid() && m_monitor->connectionState(id) != ConnectionState::Error)
                ready = false;
        }
        if (!ready && QDateTime::currentMSecsSinceEpoch() - started < 8000)
            return;
        poll->deleteLater();

        QQuickWindow* window = nullptr;
        if (m_options.page == u"countdown") {
            const auto& rules = m_context->config().rules;
            if (!rules.isEmpty())
                m_monitor->simulateRule(rules.first().id);
            m_ui->showCountdown();
            window = m_ui->countdownWindow();
        } else {
            QString device = m_options.deviceId;
            const QList<QUuid> ids = m_monitor->deviceIds();
            QUuid chosen = ids.value(0);
            for (const QUuid& id : ids) {
                const DeviceConfig* d = m_monitor->device(id);
                if (idString(id) == device || (d && d->name.compare(device, Qt::CaseInsensitive) == 0))
                    chosen = id;
            }
            // "add" and "add:<type>" open the add sheet of the devices and
            // notifications pages instead.
            const QString request = device.startsWith(u"add") ? device : idString(chosen);
            m_ui->showMain(
                m_options.page, request, m_options.size.isValid() ? m_options.size : QSize(1280, 820));
            window = m_ui->mainWindow();
        }
        if (!window) {
            qCritical("Could not create the window for the screenshot.");
            QCoreApplication::exit(1);
            return;
        }
        const int delay = m_options.delayMs >= 0 ? m_options.delayMs : 1500;
        QTimer::singleShot(delay, window, [window, file = m_options.screenshot] {
            const QImage image = window->grabWindow();
            const bool ok = !image.isNull() && image.save(file);
            if (!ok)
                qCritical("Could not save the screenshot to %s", qPrintable(file));
            QCoreApplication::exit(ok ? 0 : 1);
        });
    });
    poll->start();
}

void Application::quit()
{
    QCoreApplication::quit();
}

}  // namespace nutty
