#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace havel {
class IO;
class HotkeyManager;
struct HostContext;
void blockAllSignals();
class HavelLauncher;
}

namespace havel::automation {
class AutomationManager;
}

namespace havel::net {
class NetworkManager;
}

namespace havel {
class Modules;
}

namespace havel::compiler {
class VM;
class Scheduler;
class ExecutionEngine;
#ifdef HAVEL_ENABLE_LLVM
class JITCompiler;
#endif
}

namespace havel {

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
  void performCleanup();

  static void gracefulExit(int code = 0, bool fromSignal = false);

  // Shutdown callback for embedders (e.g., Qt event loop)
  void setShutdownCallback(std::function<void()> cb);

  // Getters
  static Havel *getInstance() { return instance; }
  bool isInitialized() const { return initialized; }
  bool isShutdownRequested() const { return shutdownRequested; }

  // Component accessors
  IO *getIO() const { return io.get(); }
  HotkeyManager *getHotkeyManager() const { return hotkeyManager.get(); }
  automation::AutomationManager *getAutomationManager() const { return automationManager.get(); }
  compiler::VM *getBytecodeVM() const { return bytecodeVM.get(); }
  // Additional getters for HavelLauncher
  HotkeyManager* getHotkeyManagerPtr() const { return hotkeyManager.get(); }
  IO* getIOPtr() const { return io.get(); }


    Modules *getModules() const { return modules_.get(); }
  compiler::ExecutionEngine *getExecutionEngine() const { return executionEngine.get(); }

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
  std::shared_ptr<HotkeyManager> hotkeyManager;
  std::shared_ptr<automation::AutomationManager> automationManager;
  std::shared_ptr<net::NetworkManager> networkManager;

  // Havel language VM
  std::unique_ptr<compiler::VM> bytecodeVM;
    std::shared_ptr<Modules> modules_;
  compiler::Scheduler *scheduler = nullptr; // Singleton-owned
  std::unique_ptr<compiler::ExecutionEngine> executionEngine;
#ifdef HAVEL_ENABLE_LLVM
  std::unique_ptr<compiler::JITCompiler> jitCompiler;
#endif



  // Host context (persistent for VM lifetime)
  std::unique_ptr<HostContext> hostContext;

  // Timer thread
  std::unique_ptr<std::thread> timerThread;
  std::atomic<bool> timerRunning{false};
  std::mutex timerMutex;
  std::condition_variable timerCv;

  // Timing
  std::chrono::steady_clock::time_point lastCheck;
  std::chrono::steady_clock::time_point lastWindowCheck;

  // State
  std::atomic<bool> initialized{false};
  std::atomic<bool> shutdownRequested{false};
  bool cleanupDone = false;
  bool guiMode{false};
  bool replMode{false};
  std::string scriptFile;
  std::vector<std::string> commandLineArgs;
};

} // namespace havel
