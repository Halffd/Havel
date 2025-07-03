#pragma once

#include <QObject>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QMenu>
#include <QIcon>
#include "qt.hpp"
#include "gui/SettingsWindow.hpp"
#include <memory>
#include <chrono>

#include "core/util/SignalWatcher.hpp"
#include "window/WindowManager.hpp"
#include "core/IO.hpp"
#include "core/ScriptEngine.hpp"
#include "core/HotkeyManager.hpp"
#include "media/MPVController.hpp"
#include <X11/Xlib.h>
namespace havel {

class HavelApp : public QObject {
    Q_OBJECT

public:
    explicit HavelApp(bool isStartup, QObject* parent = nullptr);
    ~HavelApp();

    // Non-copyable, non-movable
    HavelApp(const HavelApp&) = delete;
    HavelApp& operator=(const HavelApp&) = delete;
    HavelApp(HavelApp&&) = delete;
    HavelApp& operator=(HavelApp&&) = delete;

    bool isInitialized() const noexcept { return initialized; }

private slots:
    void onPeriodicCheck();
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void showSettings();
    void exitApp();

private:
    void setupTrayIcon();
    void initializeComponents(bool isStartup);
    void setupTimers();
    void setupSignalHandling();
    void cleanup() noexcept;

    // Components
    std::unique_ptr<IO> io;
    std::unique_ptr<WindowManager> windowManager;
    std::unique_ptr<MPVController> mpv;
    std::unique_ptr<ScriptEngine> scriptEngine;
    std::unique_ptr<HotkeyManager> hotkeyManager;
    
    // Qt components
    std::unique_ptr<QSystemTrayIcon> trayIcon;
    std::unique_ptr<QTimer> periodicTimer;
    std::unique_ptr<QMenu> trayMenu;
    
    // System components
    havel::Util::SignalWatcher signalWatcher;
    Display* display = nullptr;
    
    // Timing
    std::chrono::steady_clock::time_point lastCheck;
    std::chrono::steady_clock::time_point lastWindowCheck;
    
    // State
    bool initialized = false;
    bool shutdownRequested = false;

    static constexpr int PERIODIC_INTERVAL_MS = 50;
    static constexpr int WINDOW_CHECK_INTERVAL_MS = 300;
    static constexpr int CONFIG_CHECK_INTERVAL_S = 5;
};

} // namespace havel