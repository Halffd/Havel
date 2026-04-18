#pragma once

#include "BrightnessManager.hpp"
#include "ConfigManager.hpp"
#include "IO.hpp"
#include "HotkeyManager.hpp"
#include "modules/HostModules.hpp"
#include "window/WindowManager.hpp"
#include "extensions/gui/automation_suite/AutomationSuite.hpp"
#include <atomic>
#include <chrono>
#include <csignal>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#ifdef HAVEL_ENABLE_LLVM
#include "havel-lang/compiler/BytecodeOrcJIT.h"
#endif


// Forward declarations
#include "automation/AutomationManager.hpp"

namespace havel::net {
class NetworkManager;
}

namespace havel::gui {
class GUIManager;
}

namespace havel::compiler {
class HostBridge;
class VM;
class Scheduler;
class ExecutionEngine;
}

namespace havel {

struct HostContext;

// Block all signals in the calling thread
void blockAllSignals();

// Forward declaration
class HavelLauncher;

class Havel {
public:
  Havel(bool isStartup, std::string scriptFile, bool repl, bool gui,
        const std::vector<std::string> &args);
  ~Havel();

  // Non-copyable, non-movable
  Havel(const Havel &) = delete;
  Havel &operator=(const Havel &) = delete;
  Havel(Havel &&) = delete;
  Havel &operator=(Havel &&) = delete;

  // Core lifecycle
  void initialize(bool isStartup);
  void cleanup() noexcept;
  void exit();

  // Getters
  static Havel *getInstance() { return instance; }
  bool isInitialized() const { return initialized; }
  bool isShutdownRequested() const { return shutdownRequested; }

  // Component accessors
  IO *getIO() const { return io.get(); }
  WindowManager *getWindowManager() const { return windowManager.get(); }
  HotkeyManager *getHotkeyManager() const { return hotkeyManager.get(); }
  BrightnessManager *getBrightnessManager() const { return brightnessManager.get(); }
  AudioManager *getAudioManager() const { return audioManager.get(); }
  MPVController *getMPV() const { return mpv.get(); }
  compiler::VM *getBytecodeVM() const { return bytecodeVM.get(); }
  // Additional getters for HavelLauncher
  HotkeyManager* getHotkeyManagerPtr() const { return hotkeyManager.get(); }
  IO* getIOPtr() const { return io.get(); }
  WindowManager* getWindowManagerPtr() const { return windowManager.get(); }


  compiler::HostBridge *getHostBridge() const { return hostBridge.get(); }

  // Friend declarations for HavelLauncher
  friend class HavelLauncher;

  // Script execution
  void runScript(const std::string &scriptFile);
  void runREPL();

private:
  // Signal handling
  void setupSignalHandling();

  // Periodic checks (replaces QTimer)
  void startPeriodicTimer();
  void stopPeriodicTimer();
  void periodicLoop();

  // GUI-related (conditional)
  void showTextChunker();

  // Static instance for signal handlers
  static Havel *instance;

  // Core components
  std::shared_ptr<IO> io;
  std::shared_ptr<WindowManager> windowManager;
  std::shared_ptr<HotkeyManager> hotkeyManager;
  std::shared_ptr<BrightnessManager> brightnessManager;
  std::shared_ptr<AudioManager> audioManager;
  std::shared_ptr<MPVController> mpv;
  std::shared_ptr<automation::AutomationManager> automationManager;
  std::shared_ptr<net::NetworkManager> networkManager;

  // Havel language VM
  std::unique_ptr<compiler::VM> bytecodeVM;
  std::shared_ptr<compiler::HostBridge> hostBridge;
  compiler::Scheduler *scheduler = nullptr; // Singleton-owned
  std::unique_ptr<compiler::ExecutionEngine> executionEngine;
#ifdef HAVEL_ENABLE_LLVM
  std::unique_ptr<compiler::JITCompiler> jitCompiler;
#endif

  
  // Phase 2H-2J: Reactive hotkey system components
  class HotkeyConditionCompiler *conditionCompiler = nullptr;    // Phase 2H
  // Phase 2I: HotkeyActionWrapper is managed via static methods, not stored

  // Host context (persistent for VM lifetime)
  std::unique_ptr<HostContext> hostContext;

  // Timer thread
  std::unique_ptr<std::thread> timerThread;
  std::atomic<bool> timerRunning{false};

  // Timing
  std::chrono::steady_clock::time_point lastCheck;
  std::chrono::steady_clock::time_point lastWindowCheck;

  // State
  std::atomic<bool> initialized{false};
  std::atomic<bool> shutdownRequested{false};
  bool guiMode{false};
  bool replMode{false};
  std::string scriptFile;
  std::vector<std::string> commandLineArgs;
};

} // namespace havel
