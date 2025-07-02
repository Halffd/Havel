#include "HavelApp.hpp"
#include <QApplication>
#include <QPixmap>
#include <QTimer>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QIcon>
#include <QColor>
#include <stdexcept>
#include <iostream>

#include "AutomationSuite.hpp"
#include "core/ConfigManager.hpp"
#include "utils/Logger.hpp"

namespace havel {

HavelApp::HavelApp(bool isStartup, QObject* parent) 
    : QObject(parent)
    , lastCheck(std::chrono::steady_clock::now())
    , lastWindowCheck(std::chrono::steady_clock::now()) {
    
    try {
        setupSignalHandling();
        setupTrayIcon();
        initializeComponents(isStartup);
        setupTimers();
        initialized = true;
        info("HavelApp initialized successfully");
    } catch (const std::exception& e) {
        error("Failed to initialize HavelApp: " + std::string(e.what()));
        cleanup();
        throw;
    }
}

HavelApp::~HavelApp() {
    cleanup();
}

void HavelApp::setupTrayIcon() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        throw std::runtime_error("System tray is not available on this system");
    }

    trayIcon = std::make_unique<QSystemTrayIcon>(this);
    
    // Create icon
    QIcon icon(":/icons/havel.png");
    if (icon.isNull()) {
        // Fallback icon
        QPixmap pixmap(16, 16);
        pixmap.fill(QColor(0, 120, 215)); // Windows blue
        icon = QIcon(pixmap);
    }
    trayIcon->setIcon(icon);
    trayIcon->setToolTip("HvC - Havel Control");

    // Create context menu
    trayMenu = std::make_unique<QMenu>();
    trayMenu->addAction("Settings", this, &HavelApp::showSettings);
    trayMenu->addSeparator();
    trayMenu->addAction("Exit", this, &HavelApp::exitApp);

    trayIcon->setContextMenu(trayMenu.get());

    connect(trayIcon.get(), &QSystemTrayIcon::activated, 
            this, &HavelApp::onTrayActivated);

    trayIcon->show();
    info("System tray icon created");
}

void HavelApp::initializeComponents(bool isStartup) {
    info("Initializing HvC components...");

    // Initialize in dependency order
    io = std::make_unique<IO>();
    if (!io) {
        throw std::runtime_error("Failed to create IO manager");
    }

    windowManager = std::make_unique<WindowManager>();
    if (!windowManager) {
        throw std::runtime_error("Failed to create WindowManager");
    }
    
    mpv = std::make_unique<MPVController>();
    if (!mpv) {
        throw std::runtime_error("Failed to create MPVController");
    }
    mpv->Initialize();
    
    scriptEngine = std::make_unique<ScriptEngine>(*io, *windowManager);
    if (!scriptEngine) {
        throw std::runtime_error("Failed to create ScriptEngine");
    }
    
    hotkeyManager = std::make_unique<HotkeyManager>(*io, *windowManager, *mpv, *scriptEngine);
    if (!hotkeyManager) {
        throw std::runtime_error("Failed to create HotkeyManager");
    }

    // Initialize hotkey manager
    hotkeyManager->loadDebugSettings();
    hotkeyManager->applyDebugSettings();

    if (isStartup) {
        info("Setting startup brightness and gamma values");
        hotkeyManager->getBrightnessManager().setStartupValues();
    }

    // Register all hotkeys
    hotkeyManager->RegisterDefaultHotkeys();
    hotkeyManager->RegisterMediaHotkeys();
    hotkeyManager->RegisterWindowHotkeys();
    hotkeyManager->RegisterSystemHotkeys();
    hotkeyManager->LoadHotkeyConfigurations();

    // Print initial hotkey state
    hotkeyManager->printHotkeys();

    // Set up config watching
    havel::Configs::Get().Watch<std::string>("UI.Theme", [](const auto& oldVal, const auto& newVal) {
        info("Theme changed from " + oldVal + " to " + newVal);
    });

    // Initialize X11 display
    display = XOpenDisplay(nullptr);
    if (!display) {
        throw std::runtime_error("Failed to open X11 display");
    }

    info("All components initialized successfully");
}

void HavelApp::setupTimers() {
    periodicTimer = std::make_unique<QTimer>(this);
    connect(periodicTimer.get(), &QTimer::timeout, this, &HavelApp::onPeriodicCheck);
    periodicTimer->start(PERIODIC_INTERVAL_MS);
    info("Periodic timer started");
}

void HavelApp::setupSignalHandling() {
    try {
        havel::Util::blockAllSignals();
        signalWatcher.start();
        info("Signal handling initialized");
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to set up signal handling: " + std::string(e.what()));
    }
}

void HavelApp::onPeriodicCheck() {
    if (shutdownRequested) {
        return;
    }

    try {
        // Check for termination signals
        if (signalWatcher.shouldExitNow()) {
            info("Termination signal received. Initiating shutdown...");
            exitApp();
            return;
        }

        auto now = std::chrono::steady_clock::now();

        // Window state checks
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastWindowCheck).count() >= WINDOW_CHECK_INTERVAL_MS) {
            if (hotkeyManager) {
                hotkeyManager->checkHotkeyStates();
                
                bool isGamingWindow = hotkeyManager->evaluateCondition("currentMode == 'gaming'");
                if (isGamingWindow) {
                    hotkeyManager->grabGamingHotkeys();
                } else {
                    hotkeyManager->ungrabGamingHotkeys();
                }
            }
            lastWindowCheck = now;
        }

        // Config checks
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastCheck).count() >= CONFIG_CHECK_INTERVAL_S) {
            // Periodic config refresh logic here if needed
            lastCheck = now;
        }

    } catch (const std::exception& e) {
        error("Error in periodic check: " + std::string(e.what()));
    }
}

void HavelApp::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::DoubleClick) {
        showSettings();
    }
}

void HavelApp::showSettings() {
    try {
        AutomationSuite* suite = AutomationSuite::Instance();
        if (suite) {
            suite->showSettings();
        }
    } catch (const std::exception& e) {
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
    
    QApplication::quit();
}

void HavelApp::cleanup() noexcept {
    if (shutdownRequested) {
        return; // Already cleaning up
    }
    
    info("Starting cleanup...");
    shutdownRequested = true;

    try {
        if (periodicTimer) {
            periodicTimer->stop();
            periodicTimer.reset();
        }

        if (trayIcon) {
            trayIcon->hide();
            trayIcon.reset();
        }

        if (mpv) {
            mpv->Shutdown();
            mpv.reset();
        }

        // Reset in reverse dependency order
        hotkeyManager.reset();
        scriptEngine.reset();
        windowManager.reset();
        io.reset();

        if (display) {
            XCloseDisplay(display);
            display = nullptr;
        }

        signalWatcher.stop();
        
        info("Cleanup completed successfully");
    } catch (...) {
        // Silent cleanup - don't throw from destructor
    }
}

} // namespace havel