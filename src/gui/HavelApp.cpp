#include "HavelApp.hpp"
#include "AutomationSuite.hpp"
#include "core/BrightnessManager.hpp"
#include "core/ConfigManager.hpp"
#include "core/DisplayManager.hpp"
#include "core/ModeManager.hpp"
#include "core/io/EventListener.hpp"
#include "core/io/KeyTap.hpp"
#include "gui/GUIManager.hpp"
#include "modules/HostModules.hpp"
#include "../havel-lang/runtime/StdLibModules.hpp"
#include "utils/Logger.hpp"
#include "window/CompositorBridge.hpp"
#include "window/WindowMonitor.hpp"
#include <QApplication>
#include <QColor>
#include <QIcon>
#include <QMenu>
#include <QPixmap>
#include <QSystemTrayIcon>
#include <QTimer>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace havel {

// Block all signals in the calling thread
void blockAllSignals() {
  sigset_t set;
  sigfillset(&set);
  if (pthread_sigmask(SIG_BLOCK, &set, nullptr) != 0) {
    throw std::system_error(errno, std::system_category(),
                            "Failed to block signals");
  }
}

HavelApp::HavelApp(bool isStartup, std::string scriptFile, bool repl, bool gui,
                   const std::vector<std::string> &args, QObject *parent)
    : QObject(parent), lastCheck(std::chrono::steady_clock::now()),
      lastWindowCheck(std::chrono::steady_clock::now()) {

  if (instance) {
    throw std::runtime_error("HavelApp instance already exists");
  }
  instance = this;
  this->scriptFile = scriptFile;
  this->repl = repl;
  this->gui = gui;
  this->commandLineArgs = args;

  try {
    setupSignalHandling();
    initializeComponents(isStartup);
    setupTimers();
    initialized = true;
    info("HavelApp initialized successfully");
  } catch (const std::exception &e) {
    error("Failed to initialize HavelApp: " + std::string(e.what()));
    cleanup();
    throw;
  }
}

HavelApp::~HavelApp() {
  cleanup();
  if (instance == this) {
    instance = nullptr;
  }
  debug("HavelApp destroyed");
}

void HavelApp::cleanup() noexcept {
  debug("HavelApp::cleanup() - starting cleanup");

  // 1. Stop EventListener FIRST to prevent callbacks during cleanup
  if (io) {
    debug("HavelApp::cleanup() - stopping EventListener");
    if (io->GetEventListener()) {
      io->GetEventListener()->Stop();
    }
  }

  // 2. Destroy hotkeyManager (this will clear callbacks)
  if (hotkeyManager) {
    debug("HavelApp::cleanup() - destroying HotkeyManager");
    hotkeyManager->cleanup();
    hotkeyManager.reset();
  }

  // 3. Destroy VM FIRST (so it stops using host functions)
  if (bytecodeVM) {
    debug("HavelApp::cleanup() - destroying VM");
    bytecodeVM.reset();
  }
  
  // 4. Destroy HostBridge (now safe to clear host_functions)
  if (hostBridge) {
    debug("HavelApp::cleanup() - destroying HostBridge");
    hostBridge->shutdown();
    hostBridge.reset();
  }

  // 5. Destroy other components
  if (automationManager) {
    debug("HavelApp::cleanup() - destroying AutomationManager");
    automationManager.reset();
  }

  if (brightnessManager) {
    debug("HavelApp::cleanup() - destroying BrightnessManager");
    brightnessManager.reset();
  }

  if (audioManager) {
    debug("HavelApp::cleanup() - destroying AudioManager");
    audioManager.reset();
  }

  if (mpv) {
    debug("HavelApp::cleanup() - destroying MPVController");
    mpv.reset();
  }

  if (windowManager) {
    debug("HavelApp::cleanup() - destroying WindowManager");
    windowManager.reset();
  }

  // 5. Destroy IO LAST (after all callbacks are cleared)
  if (io) {
    debug("HavelApp::cleanup() - destroying IO");
    io.reset();
  }

  debug("HavelApp::cleanup() - cleanup complete");
}

void HavelApp::initializeComponents(bool isStartup) {
  info("Initializing HvC components...");
  info("isStartup: " + std::to_string(isStartup));
  info("GUI: ", + gui);
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
  brightnessManager->init(); // Initialize monitors after X11 is ready

  automationManager = std::make_shared<automation::AutomationManager>(io);
  if (!automationManager) {
    throw std::runtime_error("Failed to create AutomationManager");
  }

  std::cerr << "[DEBUG] AutomationManager created, initializing NetworkManager..." << std::endl;

  // Initialize NetworkManager (singleton)
  networkManager = std::shared_ptr<net::NetworkManager>(
      &net::NetworkManager::getInstance(), [](net::NetworkManager *) {});
  if (!networkManager) {
    throw std::runtime_error("Failed to create NetworkManager");
  }
  info("NetworkManager initialized successfully");
  std::cerr << "[DEBUG] NetworkManager initialized, entering ENABLE_HAVEL_LANG section..." << std::endl;

#ifdef ENABLE_HAVEL_LANG
  // Debug: Show initialization mode and parameters
  std::cerr << "[DEBUG] HavelApp initialization:" << std::endl;
  std::cerr << "  - GUI: " << (gui ? "enabled" : "disabled") << std::endl;
  std::cerr << "  - Script: " << (scriptFile.empty() ? "none" : scriptFile) << std::endl;
  std::cerr << "  - REPL: " << (repl ? "yes" : "no") << std::endl;
  std::cerr << "  - Startup: " << (isStartup ? "yes" : "no") << std::endl;
  std::cerr << "  - Command line args: " << commandLineArgs.size() << std::endl;
  
  std::cerr << "[DEBUG] Getting AutomationSuite components..." << std::endl;

  // Get AutomationSuite components with null guards
  // Pass IO to ensure proper initialization
  auto *suite = AutomationSuite::Instance(io.get());
  std::cerr << "[DEBUG] AutomationSuite obtained, getting screenshot manager..." << std::endl;
  auto *screenshotMgr = suite ? suite->getScreenshotManager() : nullptr;
  std::cerr << "[DEBUG] Screenshot manager obtained, getting clipboard manager..." << std::endl;
  auto *clipboardMgr = suite ? suite->getClipboardManager() : nullptr;
  std::cerr << "[DEBUG] Clipboard manager obtained, getting pixel automation..." << std::endl;
  auto *pixelAuto = suite ? suite->getPixelAutomation() : nullptr;
  std::cerr << "[DEBUG] Pixel automation obtained, creating WindowMonitor..." << std::endl;

  // Create WindowMonitor for efficient window info caching
  auto windowMonitor =
      std::make_shared<WindowMonitor>(std::chrono::milliseconds(100));
  std::cerr << "[DEBUG] WindowMonitor created, creating HotkeyManager..." << std::endl;

  // Get screenshot manager with null guard (nullptr in REPL mode)
  auto *screenshotMgrForHotkey =
      suite ? suite->getScreenshotManager() : nullptr;

  // Create HotkeyManager (without interpreter - conditions stubbed)
  hotkeyManager = std::make_shared<HotkeyManager>(
      io, *windowManager, *mpv, *audioManager,
      screenshotMgrForHotkey, *brightnessManager, networkManager);
  if (!hotkeyManager) {
    throw std::runtime_error("Failed to create HotkeyManager");
  }
  std::cerr << "[DEBUG] HotkeyManager created, starting window monitor..." << std::endl;

  // Start window monitor AFTER HotkeyManager is created
  windowMonitor->Start();
  std::cerr << "[DEBUG] WindowMonitor started, setting up IO..." << std::endl;
  io->setHotkeyManager(hotkeyManager);

  // Initialize hotkey manager
  hotkeyManager->loadDebugSettings();
  hotkeyManager->applyDebugSettings();
  std::cerr << "[DEBUG] HotkeyManager initialized, building HostContext..." << std::endl;

  // Build HostContext from managers (raw pointers - no ownership)
  // Use member variable to ensure it persists for lifetime of VM/HostBridge
  hostContext.io = io.get();
  hostContext.windowManager = windowManager.get();
  hostContext.hotkeyManager = hotkeyManager.get();
  hostContext.modeManager = hotkeyManager->getModeManager().get();
  hostContext.brightnessManager = brightnessManager.get();
  hostContext.audioManager = audioManager.get();
  hostContext.guiManager = guiManager.get();
  hostContext.screenshotManager = screenshotMgr;
  hostContext.clipboardManager = clipboardMgr;
  hostContext.pixelAutomation = pixelAuto;
  hostContext.automationManager = reinterpret_cast<havel::AutomationManager*>(automationManager.get());
  hostContext.fileManager = nullptr;
  hostContext.processManager = nullptr;
  hostContext.networkManager = networkManager.get();
  hostContext.windowMonitor = windowMonitor.get();
  hostContext.mpvController = mpv.get();
  std::cerr << "[DEBUG] HostContext built, initializing bytecode VM..." << std::endl;

  // Initialize bytecode VM and HostBridge
  try {
    info("Initializing bytecode VM and HostBridge...");
    std::cerr << "[DEBUG] Creating VM..." << std::endl;

    // Create VM with context
    bytecodeVM = std::make_unique<compiler::VM>(hostContext);

    // Set VM pointer in context (non-owning)
    hostContext.vm = bytecodeVM.get();

    std::cerr << "[DEBUG] Creating HostBridge..." << std::endl;

    // Create HostBridge with context
    hostBridge = compiler::createHostBridge(hostContext);

    std::cerr << "[DEBUG] Registering stdlib modules..." << std::endl;

    // Register stdlib modules with VM
    registerStdLibWithVM(*hostBridge);

    std::cerr << "[DEBUG] Installing HostBridge (this may take a moment)..." << std::endl;

    hostBridge->install();

    std::cerr << "[DEBUG] HostBridge installed successfully" << std::endl;

    info("Bytecode VM and HostBridge initialized successfully");
  } catch (const std::exception& e) {
    error("Failed to initialize bytecode VM: {}", e.what());
    std::cerr << "[DEBUG] Exception during VM initialization: " << e.what() << std::endl;
    // Continue anyway - VM is optional for now
  }
  
  // Debug: Show component initialization status
  std::cerr << "[DEBUG] Component initialization status:" << std::endl;
  std::cerr << "  - IO: " << (io ? "✓" : "✗") << std::endl;
  std::cerr << "  - WindowManager: " << (windowManager ? "✓" : "✗") << std::endl;
  std::cerr << "  - HotkeyManager: " << (hotkeyManager ? "✓" : "✗") << std::endl;
  std::cerr << "  - BytecodeVM: " << (bytecodeVM ? "✓" : "✗") << std::endl;
  std::cerr << "  - HostBridge: " << (hostBridge ? "✓" : "✗") << std::endl;
  std::cerr << "  - AudioManager: " << (audioManager ? "✓" : "✗") << std::endl;
  std::cerr << "  - BrightnessManager: " << (brightnessManager ? "✓" : "✗") << std::endl;
  std::cerr << "  - MPVController: " << (mpv ? "✓" : "✗") << std::endl;
  std::cerr << "  - NetworkManager: " << (networkManager ? "✓" : "✗") << std::endl;
  std::cerr << "  - AutomationManager: " << (automationManager ? "✓" : "✗") << std::endl;
  std::cerr << "  - WindowMonitor: " << (windowMonitor ? "✓" : "✗") << std::endl;
#else
  std::cerr << "[DEBUG] Havel language disabled" << std::endl;
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
  if (scriptFile.empty()) {
    // Hotkeys are now defined in Havel scripts (hotkeys.hv)
    // No hardcoded hotkeys in C++
    hotkeyManager->LoadHotkeyConfigurations();
  }

  if (gui) {
    io->Hotkey("@^!c", [this]() {
      QMetaObject::invokeMethod(
          this,
          []() {
            if (gui::TextChunkerWindow::instance) {
              gui::TextChunkerWindow::instance->toggleVisibility();
            }
          },
          Qt::QueuedConnection);
    });

    // Load new text
    io->Hotkey("@^!v", [this]() {
      QMetaObject::invokeMethod(
          this, [this]() { showTextChunker(); }, Qt::QueuedConnection);
    });

    // Next chunk
    io->Hotkey("@^!n", [this]() {
      QMetaObject::invokeMethod(
          this,
          []() {
            if (gui::TextChunkerWindow::instance)
              gui::TextChunkerWindow::instance->nextChunk();
          },
          Qt::QueuedConnection);
    });

    // Previous chunk
    io->Hotkey("@^!p", [this]() {
      QMetaObject::invokeMethod(
          this,
          []() {
            if (gui::TextChunkerWindow::instance)
              gui::TextChunkerWindow::instance->prevChunk();
          },
          Qt::QueuedConnection);
    });

    // Invert
    io->Hotkey("@^!i", [this]() {
      QMetaObject::invokeMethod(
          this,
          []() {
            if (gui::TextChunkerWindow::instance)
              gui::TextChunkerWindow::instance->invertMode();
          },
          Qt::QueuedConnection);
    });

    // Recopy
    io->Hotkey("@^!r", [this]() {
      QMetaObject::invokeMethod(
          this,
          []() {
            if (gui::TextChunkerWindow::instance)
              gui::TextChunkerWindow::instance->recopyChunk();
          },
          Qt::QueuedConnection);
    });

    // Increase limit
    io->Hotkey("@^!equal", [this]() {
      QMetaObject::invokeMethod(
          this,
          []() {
            if (gui::TextChunkerWindow::instance)
              gui::TextChunkerWindow::instance->increaseLimit();
          },
          Qt::QueuedConnection);
    });

    // Decrease limit
    io->Hotkey("@^!minus", [this]() {
      QMetaObject::invokeMethod(
          this,
          []() {
            if (gui::TextChunkerWindow::instance)
              gui::TextChunkerWindow::instance->decreaseLimit();
          },
          Qt::QueuedConnection);
    });
    AutomationSuite::Instance(io.get());

    // Create tray icon only in GUI mode (not REPL)
    if (!repl) {
      if (auto *suite = AutomationSuite::Instance()) {
        suite->ensureTrayIcon();
      }
    }

    // ClipboardManager is now lazy-initialized in AutomationSuite
    // No need to create it separately here
#ifdef ENABLE_HAVEL_LANG
    // Only create interpreter if it doesn't already exist
    if (false) { // interpreter removed
      guiManager = std::make_unique<GUIManager>(*windowManager);
      std::cerr << "[DEBUG] Creating interpreter..." << std::endl;

      // Get AutomationSuite components with null guards
      // Pass IO to ensure proper initialization
      auto *suite = AutomationSuite::Instance(io.get());
      auto *screenshotMgr = suite ? suite->getScreenshotManager() : nullptr;
      auto *clipboardMgr = suite ? suite->getClipboardManager() : nullptr;
      auto *pixelAuto = suite ? suite->getPixelAutomation() : nullptr;

      // Build HostContext from managers (wrap raw pointers in shared_ptr without ownership)
      HostContext ctx;
      ctx.io = io.get();  // Already shared_ptr
      ctx.windowManager = windowManager.get();
      ctx.hotkeyManager = hotkeyManager.get();
      ctx.brightnessManager = brightnessManager.get();
      ctx.audioManager = audioManager.get();
      ctx.guiManager = guiManager.get();
      ctx.screenshotManager = screenshotMgr;
      ctx.clipboardManager = clipboardMgr;
      ctx.pixelAutomation = pixelAuto;
      ctx.mpvController = mpv.get();

      // interpreter = std::make_shared<Interpreter>(ctx); // REMOVED - interpreter deleted
      // Register interpreter for hotkey callbacks (must be after construction)
      std::cerr << "[DEBUG] Interpreter created successfully" << std::endl;
    } else {
      std::cerr << "[DEBUG] Reusing existing interpreter..." << std::endl;
    }
#else
    interpreter = nullptr;
    std::cerr << "[DEBUG] Havel language disabled, interpreter is null"
              << std::endl;
#endif
  }

  if (scriptFile.empty()) {
    hotkeyManager->printHotkeys();
    hotkeyManager->updateAllConditionalHotkeys();
  }

  if (WindowManagerDetector::IsX11()) {
    // Initialize X11 display
    display = DisplayManager::GetDisplay();
    if (!display) {
      throw std::runtime_error("Failed to open X11 display");
    }
  }
}

void HavelApp::setupTimers() {
  periodicTimer = std::make_unique<QTimer>(this);
  connect(periodicTimer.get(), &QTimer::timeout, this,
          &HavelApp::onPeriodicCheck);
  periodicTimer->start(PERIODIC_INTERVAL_MS);
  info("Periodic timer started");

  // Connect to Qt's aboutToQuit signal to ensure cleanup on exit
  // Only if QApplication exists (not in pure script mode)
  if (qApp) {
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
      debug("Qt aboutToQuit signal received - forcing evdev ungrab");
      if (io) {
        io->cleanup();
      }
    });
  }
}

void HavelApp::setupSignalHandling() {
  try {
    blockAllSignals();

    // Set up fallback signal handlers for REPL mode when EventListener might
    // not be running
    if (!gui) {
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

        // Perform cleanup if possible
        // Note: In signal handlers, only async-signal-safe functions should be
        // called std::exit is async-signal-safe and will trigger cleanup via
        // atexit handlers

        std::exit(0); // Exit with success code for graceful shutdown
      };

      sigaction(SIGINT, &sa, nullptr);
      sigaction(SIGTERM, &sa, nullptr);
      sigaction(SIGABRT, &sa, nullptr);
      sigaction(SIGSEGV, &sa, nullptr);
      sigaction(SIGQUIT, &sa, nullptr);

      info("Signal handling initialized - fallback handlers for REPL mode");
    } else {
      // signalWatcher.start(); // DISABLED - EventListener handles signals
      info("Signal handling initialized - EventListener manages signals");
    }

    // Set up immediate cleanup on signal reception - prioritize evdev
    // ungrabbing
    // signalWatcher.setCleanupCallback([this]() { ... }); // Removed -
    // EventListener handles this
  } catch (const std::exception &e) {
    throw std::runtime_error("Failed to set up signal handling: " +
                             std::string(e.what()));
  }
}
void HavelApp::onPeriodicCheck() {
  if (shutdownRequested) {
    return;
  }

  try {
    // Check for termination signals - handled by EventListener now

    auto now = std::chrono::steady_clock::now();

    if (std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                              lastWindowCheck)
            .count() >= WINDOW_CHECK_INTERVAL_MS) {
      if (hotkeyManager) {
        hotkeyManager->updateAllConditionalHotkeys();
      }
      lastWindowCheck = now;
    }

    // Config checks
    if (std::chrono::duration_cast<std::chrono::seconds>(now - lastCheck)
            .count() >= CONFIG_CHECK_INTERVAL_S) {
      // Periodic config refresh logic here if needed
      lastCheck = now;
    }

  } catch (const std::exception &e) {
    error("Error in periodic check: {}", e.what());
  }
}

void HavelApp::showSettings() {
  try {
    AutomationSuite *suite = AutomationSuite::Instance();
    if (suite) {
      suite->showSettings();
    }
  } catch (const std::exception &e) {
    error("Failed to show settings: " + std::string(e.what()));
  }
}

void HavelApp::exitApp() {
  if (shutdownRequested) {
    return;
  }

  shutdownRequested = true;
  info("User requested exit - starting graceful shutdown");

  if (periodicTimer) {
    periodicTimer->stop();
  }

  // Stop EventListener FIRST before any static destructors run
  // This prevents use-after-free in KeyMap access from EventListener thread
  if (io && io->GetEventListener()) {
    info("Stopping EventListener before exit...");
    io->GetEventListener()->Stop();
    info("EventListener stopped");
  }

  // Call cleanup to stop all threads gracefully
  cleanup();

  // Hard exit to ensure all threads are killed
  info("Exit requested - terminating process");
  std::exit(0);
}

void HavelApp::showTextChunker() {
  QClipboard *clipboard = QApplication::clipboard();
  std::string text = clipboard->text().toStdString();

  if (text.empty()) {
    warn("Clipboard is empty");
    return;
  }

  if (gui::TextChunkerWindow::instance) {
    gui::TextChunkerWindow::instance->loadNewText();
  } else {
    auto *chunkerWindow = new havel::gui::TextChunkerWindow(text);
    chunkerWindow->hide(); // Start hidden
  }
}
} // namespace havel