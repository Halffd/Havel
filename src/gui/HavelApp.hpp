#pragma once

#include "core/BrightnessManager.hpp"
#include "core/automation/AutomationManager.hpp"
#include "gui/SettingsWindow.hpp"
#include "qt.hpp"
#include <QIcon>
#include <QMenu>
#include <QObject>
#include <QTimer>
#include <atomic>
#include <chrono>
#include <memory>

#ifdef ENABLE_HAVEL_LANG
#include "havel-lang/runtime/Interpreter.hpp"
#else
namespace havel {
class Interpreter;
} // namespace havel
#endif
#include "core/HotkeyManager.hpp"
#include "core/IO.hpp"
#include "core/net/NetworkManager.hpp"
#include "gui/ClipboardManager.hpp"
#include "gui/TextChunkerWindow.hpp"
#include "havel-lang/runtime/Interpreter.hpp"
#include "utils/Logger.hpp"
#include "window/WindowManager.hpp"
#include "x11.h"
namespace havel {

class HavelApp : public QObject {
  Q_OBJECT

public:
  template <typename T>
  bool MouseClick(T btnCode, int dx, int dy, int speed, float accel) {
    if (!io->MouseMove(dx, dy, speed, accel))
      return false;
    return io->EmitClick(btnCode, 2);
  }

  explicit HavelApp(bool isStartup, std::string scriptFile = "",
                    bool repl = false, bool gui = true,
                    QObject *parent = nullptr);
  ~HavelApp();

  // Non-copyable, non-movable
  HavelApp(const HavelApp &) = delete;
  HavelApp &operator=(const HavelApp &) = delete;
  HavelApp(HavelApp &&) = delete;
  HavelApp &operator=(HavelApp &&) = delete;

  bool isInitialized() const noexcept { return initialized; }
  // Singleton instance
  inline static HavelApp *instance = nullptr;
  void initializeComponents(bool isStartup);
  void setupTimers();
  void setupSignalHandling();
  void cleanup() noexcept;

  // Components
  std::string scriptFile = "";
  bool repl = false;
  bool gui = true;
#ifdef ENABLE_HAVEL_LANG
  Interpreter *getInterpreter() { return interpreter.get(); }
#else
  Interpreter *getInterpreter() { return nullptr; }
#endif
  std::shared_ptr<IO> io;
  std::shared_ptr<WindowManager> windowManager;
  std::shared_ptr<MPVController> mpv;
  std::shared_ptr<havel::Interpreter> interpreter;
  std::shared_ptr<HotkeyManager> hotkeyManager;
  std::shared_ptr<ClipboardManager> clipboardManager;
  std::shared_ptr<AudioManager> audioManager;
  std::shared_ptr<BrightnessManager> brightnessManager;
  std::shared_ptr<automation::AutomationManager> automationManager;
  std::shared_ptr<net::NetworkManager> networkManager;
#ifdef ENABLE_HAVEL_LANG
  std::unique_ptr<GUIManager> guiManager;
#endif
private slots:
  void showTextChunker();
  void onPeriodicCheck();
  void showSettings();
  void exitApp();

private:
  // Qt components
  std::unique_ptr<QTimer> periodicTimer;
  std::unique_ptr<QTimer> windowCheckTimer;
  std::unique_ptr<QTimer> configCheckTimer;

  // System components
  Display *display = nullptr;

  // Time tracking
  std::chrono::steady_clock::time_point lastCheck;
  std::chrono::steady_clock::time_point lastWindowCheck;

  // State
  bool initialized = false;
  std::atomic<bool> shutdownRequested{false};

  static constexpr int PERIODIC_INTERVAL_MS = 50;
  static constexpr int WINDOW_CHECK_INTERVAL_MS = 100;
  static constexpr int CONFIG_CHECK_INTERVAL_S = 5;
};

} // namespace havel