#pragma once

#include <QObject>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QMenu>
#include <QIcon>
#include "core/BrightnessManager.hpp"
#include "qt.hpp"
#include "gui/SettingsWindow.hpp"
#include <memory>
#include <chrono>

#include "core/util/SignalWatcher.hpp"
#include "runtime/Interpreter.hpp"
#include "window/WindowManager.hpp"
#include "core/IO.hpp"
#include "core/ScriptEngine.hpp"
#include "core/HotkeyManager.hpp"
#include "media/MPVController.hpp"
#include "media/AudioManager.hpp"
#include "x11.h"
#include "gui/ClipboardManager.hpp"
#include "gui/TextChunkerWindow.hpp"
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
// Singleton instance
    inline static HavelApp* instance = nullptr;
    void setupTrayIcon();
    void initializeComponents(bool isStartup);
    void setupTimers();
    void setupSignalHandling();
    void cleanup() noexcept;
    
    // Components
    Interpreter* getInterpreter() { return interpreter.get(); }
    std::shared_ptr<IO> io;
    std::shared_ptr<WindowManager> windowManager;
    std::shared_ptr<MPVController> mpv;
    std::shared_ptr<ScriptEngine> scriptEngine;
    std::shared_ptr<HotkeyManager> hotkeyManager;
    std::shared_ptr<ClipboardManager> clipboardManager;
    std::shared_ptr<AudioManager> audioManager;
    std::shared_ptr<BrightnessManager> brightnessManager;
    std::shared_ptr<GUIManager> guiManager;
    std::shared_ptr<QMenu> trayMenu;
    
private slots:
    void showTextChunker();
    void onPeriodicCheck();
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void showSettings();
    void exitApp();
    
    private:
    // Qt components
    std::unique_ptr<QSystemTrayIcon> trayIcon;
    std::unique_ptr<QTimer> periodicTimer;
    
    // System components
    util::SignalWatcher signalWatcher;
    Display* display = nullptr;
    
    // Timing
    std::chrono::steady_clock::time_point lastCheck;
    std::chrono::steady_clock::time_point lastWindowCheck;
    
    // State
    bool initialized = false;
    bool shutdownRequested = false;

    std::unique_ptr<Interpreter> interpreter;
    static constexpr int PERIODIC_INTERVAL_MS = 50;
    static constexpr int WINDOW_CHECK_INTERVAL_MS = 100;
    static constexpr int CONFIG_CHECK_INTERVAL_S = 5;
};

} // namespace havel