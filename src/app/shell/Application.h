// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include "core/model/Config.h"

#include <QObject>
#include <QPointer>
#include <QSize>
#include <QString>
#include <QTimer>

#include <memory>

class QTemporaryDir;
class QTranslator;

namespace nutty {

class ActionRunner;
class AppContext;
class ConfigStore;
class Dispatcher;
class HistoryStore;
class Monitor;
class SecretStore;
class SingleInstance;
class SleepWatcher;
class SystemAppearance;
class TrayController;
class UiHost;

// The command line.
struct Options {
    bool hidden = false;  // start in the tray
    bool dryRun = false;  // simulate every power action
    QString configDir;  // config, history, secrets and images in one directory
    QString screenshot;  // render a page to this file, then exit
    QString page;  // home, history, device, devices, notifications, actions, general, about, countdown
    QString deviceId;  // for page "device" (id or name); default: the first device
    QString theme;  // "dark" or "light" to override the setting
    QSize size;
    int delayMs = -1;  // extra wait before the screenshot
};

// Parses argv (handles --help and --version itself, which exit).
Options parseOptions();
// True when argv asks for a screenshot; checked before QApplication exists,
// to render offscreen.
bool wantsScreenshot(int argc, char** argv);

// Wires the backend to the UI: single instance, stores, monitor and
// dispatcher, sleep/wake, appearance, translations, the tray, the windows,
// and carrying out power actions.
class Application : public QObject
{
    Q_OBJECT

public:
    explicit Application(Options options, QObject* parent = nullptr);
    ~Application() override;

    // Returns the exit code.
    int run();

private:
    bool startServices();
    void setupTray();
    void createTray();
    void applyGeneral(const GeneralSettings& general, const GeneralSettings* previous);
    void installTranslators(const QString& language);
    void onPendingChanged();
    void onMessage(const QByteArray& message);
    void updateActivationPolicy();
    void prune();
    void takeScreenshot();
    void quit();

    Options m_options;
    QString m_configDir;
    QString m_dataDir;
    bool m_firstRun = false;
    std::unique_ptr<QTemporaryDir> m_scratch;
    std::unique_ptr<SingleInstance> m_instance;
    std::unique_ptr<SecretStore> m_secrets;
    std::unique_ptr<ConfigStore> m_configStore;
    std::unique_ptr<HistoryStore> m_history;
    std::unique_ptr<Monitor> m_monitor;
    std::unique_ptr<Dispatcher> m_dispatcher;
    std::unique_ptr<ActionRunner> m_actions;
    std::unique_ptr<AppContext> m_context;
    std::unique_ptr<SleepWatcher> m_sleep;
    std::unique_ptr<SystemAppearance> m_appearance;
    std::unique_ptr<TrayController> m_tray;
    std::unique_ptr<UiHost> m_ui;
    QList<QTranslator*> m_translators;
    QString m_language;
    QTimer m_pruneTimer;
    QTimer m_trayGrace;
    QObject* m_trayWatcher = nullptr;
    QString m_countdownKey;  // the pending action the countdown was shown for
};

}  // namespace nutty
