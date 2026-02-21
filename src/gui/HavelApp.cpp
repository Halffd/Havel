#include "HavelApp.hpp"
#include "AutomationSuite.hpp"
#include "core/BrightnessManager.hpp"
#include "core/ConfigManager.hpp"
#include "core/DisplayManager.hpp"
#include "core/io/EventListener.hpp" // Include EventListener for access to its members
#include "core/io/KeyTap.hpp"
#include "gui/GUIManager.hpp"
#include "utils/Logger.hpp"
#include "window/CompositorBridge.hpp"
#include <QApplication>
#include <QColor>
#include <QIcon>
#include <QMenu>
#include <QPixmap>
#include <QSystemTrayIcon>
#include <QTimer>
#include <cstdlib>
#include <csignal>
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
                   QObject *parent)
    : QObject(parent), lastCheck(std::chrono::steady_clock::now()),
      lastWindowCheck(std::chrono::steady_clock::now()) {

  if (instance) {
    throw std::runtime_error("HavelApp instance already exists");
  }
  instance = this;
  this->scriptFile = scriptFile;
  this->repl = repl;
  this->gui = gui;

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
  std::cerr << "[DEBUG] Creating interpreter..." << std::endl;
  interpreter = std::make_unique<Interpreter>(
      *io, *windowManager, hotkeyManager.get(), brightnessManager.get(),
      audioManager.get(), guiManager.get(),
      AutomationSuite::Instance()->getScreenshotManager());
  if (!interpreter) {
    throw std::runtime_error("Failed to create Interpreter");
  }
  std::cerr << "[DEBUG] Interpreter created successfully" << std::endl;
#else
  interpreter = nullptr;
  std::cerr << "[DEBUG] Havel language disabled, interpreter is null"
            << std::endl;
#endif
  info("Havel interpreter initialized successfully");

  hotkeyManager = std::make_shared<HotkeyManager>(
      *io, *windowManager, *mpv, *audioManager, *interpreter,
      *AutomationSuite::Instance()->getScreenshotManager(), *brightnessManager,
      networkManager);
  if (!hotkeyManager) {
    throw std::runtime_error("Failed to create HotkeyManager");
  }

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
    // Register all hotkeys
    hotkeyManager->RegisterDefaultHotkeys();
    hotkeyManager->RegisterMediaHotkeys();
    hotkeyManager->RegisterWindowHotkeys();
    hotkeyManager->RegisterSystemHotkeys();
    hotkeyManager->registerAutomationHotkeys();
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

    try {
      if (!io) {
        throw std::runtime_error("IO system not available");
      }

      info("Initializing ClipboardManager...");
      clipboardManager = std::make_unique<ClipboardManager>(io.get());

      // Verify clipboard manager was created successfully
      if (!clipboardManager) {
        throw std::runtime_error("Failed to create ClipboardManager instance");
      }

      // Initialize hotkeys
      clipboardManager->initializeHotkeys();
    } catch (const std::exception &e) {
      std::string errorMsg =
          std::string("Failed to initialize ClipboardManager: ") + e.what();
      error(errorMsg);
    }
#ifdef ENABLE_HAVEL_LANG
    guiManager = std::make_unique<GUIManager>(*windowManager);
    std::cerr << "[DEBUG] Creating interpreter..." << std::endl;
    interpreter = std::make_unique<Interpreter>(
        *io, *windowManager, hotkeyManager.get(), brightnessManager.get(),
        audioManager.get(), guiManager.get(),
        AutomationSuite::Instance()->getScreenshotManager());
    std::cerr << "[DEBUG] Interpreter created successfully" << std::endl;
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
}

void HavelApp::setupSignalHandling() {
  try {
    blockAllSignals();
    // signalWatcher.start(); // DISABLED - EventListener handles signals
    info("Signal handling initialized - EventListener manages signals");

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

  // Call cleanup to stop all threads gracefully
  cleanup();
  
  // Hard exit to ensure all threads are killed
  info("Exit requested - terminating process");
  std::exit(0);
}

void HavelApp::cleanup() noexcept {
  if (shutdownRequested) {
    return; // Already cleaning up
  }

  shutdownRequested = true;

  // Clean up components in reverse order of initialization
  // Reset in order to ensure proper cleanup sequence
  clipboardManager.reset();

  // Reset hotkey manager safely - this will trigger its cleanup
  if (hotkeyManager) {
    hotkeyManager->cleanup(); // Call cleanup directly
    hotkeyManager.reset();
  }

  mpv.reset();

  // Shutdown compositor bridge before window manager
  WindowManager::ShutdownCompositorBridge();

  windowManager.reset();

  // Reset IO safely
  if (io) {
    io->cleanup();
    io.reset();
  }

  info("HavelApp cleanup complete");
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