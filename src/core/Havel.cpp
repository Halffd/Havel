#include "Havel.hpp"
#include "havel-lang/runtime/StdLibModules.hpp"
#include "gui/AutomationSuite.hpp"
#include "BrightnessManager.hpp"
#include "ConfigManager.hpp"
#include "DisplayManager.hpp"
#include "ModeManager.hpp"
#include "media/MPVController.hpp"
#include "media/AudioManager.hpp"
#include "io/EventListener.hpp"
#include "io/KeyTap.hpp"
#include "modules/HostModules.hpp"
#include "utils/Logger.hpp"
#include "window/CompositorBridge.hpp"
#include "window/WindowMonitor.hpp"
#include "net/NetworkManager.hpp"
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace havel {

// Static instance pointer
Havel *Havel::instance = nullptr;

// Constants for timing
constexpr int PERIODIC_INTERVAL_MS = 100;
constexpr int WINDOW_CHECK_INTERVAL_MS = 100;
constexpr int CONFIG_CHECK_INTERVAL_S = 5;

// Block all signals in the calling thread
void blockAllSignals() {
  sigset_t set;
  sigfillset(&set);
  if (pthread_sigmask(SIG_BLOCK, &set, nullptr) != 0) {
    throw std::system_error(errno, std::system_category(),
                            "Failed to block signals");
  }
}

Havel::Havel(bool isStartup, std::string scriptFile, bool repl, bool gui,
             const std::vector<std::string> &args)
    : lastCheck(std::chrono::steady_clock::now()),
      lastWindowCheck(std::chrono::steady_clock::now()),
      guiMode(gui),
      replMode(repl),
      scriptFile(scriptFile),
      commandLineArgs(args) {

  if (instance) {
    throw std::runtime_error("Havel instance already exists");
  }
  instance = this;

  try {
    setupSignalHandling();
    initialize(isStartup);
    startPeriodicTimer();
    initialized = true;
    info("Havel initialized successfully");
  } catch (const std::exception &e) {
    error("Failed to initialize Havel: " + std::string(e.what()));
    cleanup();
    throw;
  }
}

Havel::~Havel() {
  cleanup();
  if (instance == this) {
    instance = nullptr;
  }
  debug("Havel destroyed");
}

void Havel::initialize(bool isStartup) {
  info("Initializing HvC components...");
  info("isStartup: " + std::to_string(isStartup));
  info("GUI: " + std::to_string(guiMode));

  // Initialize in dependency order
  io = std::make_shared<IO>();
  if (!io) {
    throw std::runtime_error("Failed to create IO manager");
  }

  windowManager = std::make_shared<WindowManager>();
  if (!windowManager) {
    throw std::runtime_error("Failed to create WindowManager");
  }

  // Initialize compositor bridge
  WindowManager::InitializeCompositorBridge();

  mpv = std::make_shared<MPVController>();
  if (!mpv) {
    throw std::runtime_error("Failed to create MPVController");
  }
  mpv->Initialize();

  audioManager = std::make_shared<AudioManager>(AudioBackend::AUTO);
  if (!audioManager) {
    throw std::runtime_error("Failed to create AudioManager");
  }

  brightnessManager = std::make_shared<BrightnessManager>();
  if (!brightnessManager) {
    throw std::runtime_error("Failed to create BrightnessManager");
  }
  brightnessManager->init();

  automationManager = std::make_shared<automation::AutomationManager>(io);
  if (!automationManager) {
    throw std::runtime_error("Failed to create AutomationManager");
  }

  // Initialize NetworkManager (singleton)
  networkManager = std::shared_ptr<net::NetworkManager>(
      &net::NetworkManager::getInstance(), [](net::NetworkManager *) {});
  if (!networkManager) {
    throw std::runtime_error("Failed to create NetworkManager");
  }
  info("NetworkManager initialized successfully");

#ifdef ENABLE_HAVEL_LANG
  info("Initializing bytecode VM and HostBridge...");

  // Create host context
  hostContext = std::make_unique<HostContext>();
  hostContext->io = io.get();
  hostContext->windowManager = windowManager.get();
  hostContext->hotkeyManager = hotkeyManager.get();
  hostContext->brightnessManager = brightnessManager.get();
  hostContext->audioManager = audioManager.get();
  hostContext->networkManager = networkManager.get();
  hostContext->mpvController = mpv.get();

  // Create VM
  bytecodeVM = std::make_unique<compiler::VM>(*hostContext);
  hostContext->vm = bytecodeVM.get();

  // Create HostBridge
  hostBridge = compiler::createHostBridge(*hostContext);

  // Register stdlib modules
  registerStdLibWithVM(*hostBridge);
  hostBridge->install();

  info("Bytecode VM and HostBridge initialized successfully");
#else
  info("Havel language disabled");
#endif

  info("Havel initialized successfully");

  if (isStartup) {
    TimerManager::SetTimer(
        Configs::Get().Get<int>("Display.StartupDelayMs", 10000),
        [this]() {
          info("Setting startup brightness and gamma values");
          brightnessManager->setBrightness(
              Configs::Get().Get<double>("Display.StartupBrightness", 0.4));
          brightnessManager->setTemperature(
              Configs::Get().Get<int>("Display.StartupTemperature", 5500));
        },
        false);
  }

  if (WindowManagerDetector::IsX11()) {
    Display *display = DisplayManager::GetDisplay();
    if (!display) {
      throw std::runtime_error("Failed to open X11 display");
    }
  }
}

void Havel::cleanup() noexcept {
  debug("Havel::cleanup() - starting cleanup");

  // Stop timer first
  stopPeriodicTimer();

  // Stop EventListener FIRST
  if (io && io->GetEventListener()) {
    debug("Havel::cleanup() - stopping EventListener");
    io->GetEventListener()->Stop();
  }

  // Destroy hotkeyManager
  if (hotkeyManager) {
    debug("Havel::cleanup() - destroying HotkeyManager");
    hotkeyManager->cleanup();
    hotkeyManager.reset();
  }

  // Destroy VM FIRST
  if (bytecodeVM) {
    debug("Havel::cleanup() - destroying VM");
    bytecodeVM.reset();
  }

  // Destroy HostBridge
  if (hostBridge) {
    debug("Havel::cleanup() - destroying HostBridge");
    hostBridge->shutdown();
    hostBridge.reset();
  }

  // Destroy other components
  if (automationManager) {
    automationManager.reset();
  }
  if (brightnessManager) {
    brightnessManager.reset();
  }
  if (audioManager) {
    audioManager.reset();
  }
  if (mpv) {
    mpv.reset();
  }
  if (windowManager) {
    windowManager.reset();
  }

  // Destroy IO LAST
  if (io) {
    io->cleanup();
    io.reset();
  }

  debug("Havel::cleanup() - cleanup complete");
}

void Havel::exit() {
  if (shutdownRequested) {
    return;
  }

  shutdownRequested = true;
  info("Exit requested - starting graceful shutdown");

  // Stop EventListener FIRST
  if (io && io->GetEventListener()) {
    info("Stopping EventListener before exit...");
    io->GetEventListener()->Stop();
    info("EventListener stopped");
  }

  cleanup();

  info("Exit requested - terminating process");
  std::exit(0);
}

void Havel::setupSignalHandling() {
  try {
    blockAllSignals();

    // Set up fallback signal handlers for REPL mode
    if (!guiMode) {
      struct sigaction sa;
      sa.sa_flags = 0;
      sigemptyset(&sa.sa_mask);
      sa.sa_handler = [](int sig) {
        switch (sig) {
        case SIGINT:
          info("Received SIGINT (Ctrl+C) - shutting down gracefully");
          break;
        case SIGTERM:
          info("Received SIGTERM - shutting down gracefully");
          break;
        case SIGQUIT:
          info("Received SIGQUIT - shutting down gracefully");
          break;
        case SIGABRT:
          info("Received SIGABRT - aborting");
          break;
        case SIGSEGV:
          info("Received SIGSEGV - segmentation fault");
          break;
        default:
          info("Received signal {} - shutting down", sig);
          break;
        }
        std::exit(0);
      };

      sigaction(SIGINT, &sa, nullptr);
      sigaction(SIGTERM, &sa, nullptr);
      sigaction(SIGABRT, &sa, nullptr);
      sigaction(SIGSEGV, &sa, nullptr);
      sigaction(SIGQUIT, &sa, nullptr);

      info("Signal handling initialized - fallback handlers for REPL mode");
    } else {
      info("Signal handling initialized - EventListener manages signals");
    }
  } catch (const std::exception &e) {
    throw std::runtime_error("Failed to set up signal handling: " +
                             std::string(e.what()));
  }
}

void Havel::startPeriodicTimer() {
  timerRunning = true;
  timerThread = std::make_unique<std::thread>([this]() {
    while (timerRunning) {
      auto now = std::chrono::steady_clock::now();

      // Window check
      if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastWindowCheck)
              .count() >= WINDOW_CHECK_INTERVAL_MS) {
        if (hotkeyManager) {
          hotkeyManager->updateAllConditionalHotkeys();
        }
        lastWindowCheck = now;
      }

      // Config check
      if (std::chrono::duration_cast<std::chrono::seconds>(now - lastCheck)
              .count() >= CONFIG_CHECK_INTERVAL_S) {
        lastCheck = now;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(PERIODIC_INTERVAL_MS));
    }
  });
}

void Havel::stopPeriodicTimer() {
  timerRunning = false;
  if (timerThread && timerThread->joinable()) {
    timerThread->join();
  }
  timerThread.reset();
}

void Havel::runScript(const std::string &scriptFile) {
  // Script execution via VM
  if (!bytecodeVM) {
    throw std::runtime_error("VM not initialized");
  }
  // Implementation would call VM to execute script
  info("Running script: " + scriptFile);
}

void Havel::runREPL() {
  if (!bytecodeVM) {
    throw std::runtime_error("VM not initialized");
  }
  info("Starting REPL...");
  // REPL implementation would go here
}

} // namespace havel
