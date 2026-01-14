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
#include <atomic>

#include "core/util/SignalWatcher.hpp"
#ifdef ENABLE_HAVEL_LANG
#include "havel-lang/runtime/Interpreter.hpp"
#else
class Interpreter;  // Forward declaration when Havel Lang is disabled
#endif
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
    explicit HavelApp(bool isStartup, std::string scriptFile = "", bool repl = false, bool gui = true, QObject* parent = nullptr);
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
    std::string scriptFile = "";
    bool repl = false;
    bool gui = true;
    #ifdef ENABLE_HAVEL_LANG
    Interpreter* getInterpreter() { return interpreter.get(); }
    #else
    Interpreter* getInterpreter() { return nullptr; }
    #endif
    std::shared_ptr<IO> io;
    std::shared_ptr<WindowManager> windowManager;
    std::shared_ptr<MPVController> mpv;
    std::shared_ptr<ScriptEngine> scriptEngine;
    std::shared_ptr<HotkeyManager> hotkeyManager;
    std::shared_ptr<ClipboardManager> clipboardManager;
    std::shared_ptr<AudioManager> audioManager;
    std::shared_ptr<BrightnessManager> brightnessManager;
    // std::shared_ptr<GUIManager> guiManager;  // GUIManager is not defined - commenting out
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
    std::atomic<bool> shutdownRequested{false};

    #ifdef ENABLE_HAVEL_LANG
    std::unique_ptr<Interpreter> interpreter;
    #endif
    static constexpr int PERIODIC_INTERVAL_MS = 50;
    static constexpr int WINDOW_CHECK_INTERVAL_MS = 100;
    static constexpr int CONFIG_CHECK_INTERVAL_S = 5;
};

} // namespace havel