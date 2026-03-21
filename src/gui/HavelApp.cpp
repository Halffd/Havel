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

  // 3. Destroy interpreter (which holds HostAPI)
  if (interpreter) {
    debug("HavelApp::cleanup() - destroying Interpreter");
    interpreter.reset();
  }

  // 4. Destroy other components
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

  // Initialize NetworkManager (singleton)
  networkManager = std::shared_ptr<net::NetworkManager>(
      &net::NetworkManager::getInstance(), [](net::NetworkManager *) {});
  if (!networkManager) {
    throw std::runtime_error("Failed to create NetworkManager");
  }
  info("NetworkManager initialized successfully");

#ifdef ENABLE_HAVEL_LANG
  std::cerr << "[DEBUG] Creating interpreter (without HotkeyManager)..."
            << std::endl;

  // Get AutomationSuite components with null guards
  auto *suite = AutomationSuite::Instance();
  auto *screenshotMgr = suite ? suite->getScreenshotManager() : nullptr;
  auto *clipboardMgr = suite ? suite->getClipboardManager() : nullptr;
  auto *pixelAuto = suite ? suite->getPixelAutomation() : nullptr;

  // Create WindowMonitor for efficient window info caching
  auto windowMonitor =
      std::make_shared<WindowMonitor>(std::chrono::milliseconds(100));

  // Build HostContext from managers (hotkeyManager will be set later)
  HostContext ctx;
  ctx.commandLineArgs = commandLineArgs; // Store command line arguments
  ctx.io = io;                           // Share ownership
  ctx.windowManager = windowManager.get();
  ctx.hotkeyManager = nullptr;       // Will be set after HotkeyManager creation
  ctx.modeManager = nullptr;         // Will be set after HotkeyManager creation
  ctx.windowMonitor = windowMonitor; // Window monitoring
  ctx.brightnessManager = brightnessManager.get();
  ctx.audioManager = audioManager.get();
  ctx.guiManager = guiManager.get();
  ctx.screenshotManager = screenshotMgr;
  ctx.clipboardManager = clipboardMgr;
  ctx.pixelAutomation = pixelAuto;

  interpreter = std::make_shared<Interpreter>(ctx);
  if (!interpreter) {
    throw std::runtime_error("Failed to create Interpreter");
  }

  std::cerr << "[DEBUG] Creating HotkeyManager..." << std::endl;

  // Get screenshot manager with null guard (nullptr in REPL mode)
  auto *screenshotMgrForHotkey =
      suite ? suite->getScreenshotManager() : nullptr;

  // Create HotkeyManager - needs interpreter reference
  hotkeyManager = std::make_shared<HotkeyManager>(
      io, *windowManager, *mpv, *audioManager, *interpreter,
      screenshotMgrForHotkey, *brightnessManager, networkManager);
  if (!hotkeyManager) {
    throw std::runtime_error("Failed to create HotkeyManager");
  }

  // Start window monitor AFTER HotkeyManager is created
  windowMonitor->Start();
  io->setHotkeyManager(hotkeyManager);

  // Initialize hotkey manager
  hotkeyManager->loadDebugSettings();
  hotkeyManager->applyDebugSettings();

  // Update interpreter's hostContext with hotkeyManager and modeManager
  interpreter->getHostContext().hotkeyManager = hotkeyManager;
  interpreter->getHostContext().modeManager = hotkeyManager->getModeManager();

  // Update HostAPI with hotkeyManager and modeManager (HostAPI was created with
  // nullptr)
  if (interpreter->getHostAPI()) {
    interpreter->getHostAPI()->SetHotkeyManager(hotkeyManager.get());
    interpreter->getHostAPI()->SetModeManager(
        hotkeyManager->getModeManager().get());
  }

  // Set interpreter in hotkeyManager for condition evaluation
  hotkeyManager->setInterpreter(interpreter.get());

  // Register interpreter for hotkey callbacks (must be after construction)
  interpreter->RegisterForHotkeys();
  std::cerr << "[DEBUG] Interpreter created successfully" << std::endl;

  // Initialize bytecode VM and HostBridge
  try {
    info("Initializing bytecode VM and HostBridge...");
    
    // Initialize service registry with all services
    initializeServiceRegistry(interpreter->getHostAPI());
    
    // Create VM
    bytecodeVM = std::make_unique<compiler::VM>();

    // Create HostBridge dependencies (pass VM* for ModeService)
    auto deps = createHostBridgeDependencies(interpreter->getHostAPI(), bytecodeVM.get());

    // Create HostBridge registry
    hostBridgeRegistry = compiler::createHostBridgeRegistry(*bytecodeVM, deps);

    // Register stdlib modules with VM (VM-native)
    registerStdLibWithVM(*hostBridgeRegistry);

    info("Bytecode VM and HostBridge initialized successfully");
  } catch (const std::exception& e) {
    error("Failed to initialize bytecode VM: {}", e.what());
    // Continue anyway - VM is optional for now
  }
#else
  interpreter = nullptr;
  std::cerr << "[DEBUG] Havel language disabled, interpreter is null"
            << std::endl;
#endif
  info("Havel interpreter initialized successfully");

  // Set the hotkeyManager on the IO instance so it can access it during
  // suspend/resume operations
  io->setHotkeyManager(hotkeyManager);

  // Initialize hotkey manager
  hotkeyManager->loadDebugSettings();
  hotkeyManager->applyDebugSettings();

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
    if (!interpreter) {
      guiManager = std::make_unique<GUIManager>(*windowManager);
      std::cerr << "[DEBUG] Creating interpreter..." << std::endl;

      // Get AutomationSuite components with null guards
      auto *suite = AutomationSuite::Instance();
      auto *screenshotMgr = suite ? suite->getScreenshotManager() : nullptr;
      auto *clipboardMgr = suite ? suite->getClipboardManager() : nullptr;
      auto *pixelAuto = suite ? suite->getPixelAutomation() : nullptr;

      // Build HostContext from managers
      HostContext ctx;
      ctx.io = io; // Share ownership
      ctx.windowManager = windowManager.get();
      ctx.hotkeyManager = hotkeyManager;
      ctx.brightnessManager = brightnessManager.get();
      ctx.audioManager = audioManager.get();
      ctx.guiManager = guiManager.get();
      ctx.screenshotManager = screenshotMgr;
      ctx.clipboardManager = clipboardMgr;
      ctx.pixelAutomation = pixelAuto;

      interpreter = std::make_shared<Interpreter>(ctx);
      // Register interpreter for hotkey callbacks (must be after construction)
      interpreter->RegisterForHotkeys();
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