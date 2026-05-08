#include "Havel.hpp"
#include "havel-lang/runtime/StdLibModules.hpp"
#include "havel-lang/runtime/concurrency/Scheduler.hpp"
#include "havel-lang/runtime/execution/ExecutionEngine.hpp"
#include "HotkeyConditionCompiler.hpp"
#include "HotkeyActionWrapper.hpp"
#include "HotkeyActionContext.hpp"
#include "extensions/gui/automation_suite/AutomationSuite.hpp"
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
#include "core/util/Env.hpp"
#include "window/CompositorBridge.hpp"
#include "window/WindowMonitor.hpp"
#include "net/NetworkManager.hpp"
#include "host/ServiceRegistry.hpp"
#include "host/image/ImageService.hpp"
#include <csignal>
#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <filesystem>
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
  debug("Initializing HvC components...");
  debug("isStartup: " + std::to_string(isStartup));
  debug("GUI: " + std::to_string(guiMode));

  // Initialize in dependency order
  io = std::make_shared<IO>();
  if (!io) {
    throw std::runtime_error("Failed to create IO manager");
  }

  // Create HotkeyManager (depends on IO)
  hotkeyManager = std::make_shared<HotkeyManager>(io);
  if (!hotkeyManager) {
    throw std::runtime_error("Failed to create HotkeyManager");
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

#ifdef ENABLE_HAVEL_LANG
  debug("Initializing bytecode VM and HostBridge...");

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

#ifdef HAVEL_ENABLE_LLVM
  // Initialize JIT if enabled
  bool useJIT = Configs::Get().Get<bool>("Compiler.JIT", true);
  // Override with CLI config if present (assuming this->commandLineArgs or similar)
  // For now we check the instance-level config if we had it, but Havre uses LaunchConfig in Launcher.
  // We'll trust the ConfigManager which should be updated by Launcher.
  
  if (useJIT) {
    debug("JIT compilation enabled");
    auto jit = std::make_unique<compiler::BytecodeOrcJIT>();
    
    // Check for debug JIT
    if (Configs::Get().Get<bool>("Compiler.DebugJIT", false)) {
      jit->setDebugMode(true);
    }
    
    // Check for assembly dumping
    if (Configs::Get().Get<bool>("Compiler.OutputAsm", false)) {
      jit->setDumpAsmToFile(true);
    }
    
    // Check for IR dumping
    if (Configs::Get().Get<bool>("Compiler.DumpIR", false)) {
      jit->setDumpIR(true);
    }

    jit->setShowWarnings(Configs::Get().Get<bool>("Compiler.JITWarnings", true));
    std::string jitTargetOS = Configs::Get().Get<std::string>("Compiler.JITTargetOS", "");
    std::transform(jitTargetOS.begin(), jitTargetOS.end(), jitTargetOS.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (jitTargetOS == "linux") {
      jit->setTargetOS(compiler::BytecodeOrcJIT::TargetOS::Linux);
    } else if (jitTargetOS == "windows" || jitTargetOS == "win") {
      jit->setTargetOS(compiler::BytecodeOrcJIT::TargetOS::Windows);
    } else if (jitTargetOS == "macos" || jitTargetOS == "darwin" || jitTargetOS == "mac") {
      jit->setTargetOS(compiler::BytecodeOrcJIT::TargetOS::MacOS);
    } else if (jitTargetOS == "wasm") {
      jit->setTargetOS(compiler::BytecodeOrcJIT::TargetOS::Wasm);
    } else {
      jit->setTargetOS(compiler::BytecodeOrcJIT::TargetOS::Native);
    }

    // Hook up to VM
  bytecodeVM->setHotFunctionCallback([jit_ptr = jit.get()](const compiler::BytecodeFunction& func) {
    if (!jit_ptr->isCompiled(func.name)) {
      jit_ptr->compileFunction(func);
    }
  });
  bytecodeVM->setJITCompiler(jit.get());
    
    // Store JIT in some way? Or just keep it alive?
    // We'll need a member in Havel to keep the JIT instance alive.
    this->jitCompiler = std::move(jit);

  }
#endif


  // Create HostBridge
  hostBridge = compiler::createHostBridge(*hostContext);

// Register stdlib modules
registerStdLibWithVM(*hostBridge);
hostBridge->install();

{
    std::string stdlibPath;
    const char* envStdlib = std::getenv("HAVEL_STDLIB");
    if (envStdlib && envStdlib[0] != '\0') {
        stdlibPath = envStdlib;
    } else {
        auto exePath = Env::executable();
        if (!exePath.empty()) {
            stdlibPath = (std::filesystem::path(exePath).parent_path() / ".." / "stdlib").string();
        } else {
            stdlibPath = "./stdlib";
        }
    }
        bytecodeVM->moduleLoader().setStdlibPath(stdlibPath);
    }

    {
        auto exePath = Env::executable();
        if (!exePath.empty()) {
            auto modulesPath = std::filesystem::path(exePath).parent_path()
                / ".." / "modules";
            if (std::filesystem::exists(modulesPath)) {
                bytecodeVM->moduleLoader().addSearchPath(
                    std::filesystem::canonical(modulesPath).string());
            }
        }
    }


  scheduler = &compiler::Scheduler::instance();
  if (!scheduler) {
    throw std::runtime_error("Failed to create Scheduler");
  }

  // EventQueue is created by ConcurrencyBridge during hostBridge->install()
  compiler::EventQueue* eventQueue = hostContext->eventQueue;
  if (!eventQueue) {
    throw std::runtime_error("Failed to get EventQueue from HostBridge");
  }

  // Create ExecutionEngine for main loop integration
	executionEngine = std::make_unique<compiler::ExecutionEngine>(
		bytecodeVM.get(), scheduler, eventQueue);
	if (!executionEngine) {
		throw std::runtime_error("Failed to create ExecutionEngine");
	}

	// Wire VM to WatcherRegistry and Scheduler for reactive when
	bytecodeVM->setWatcherRegistry(executionEngine->getWatcherRegistry());
	bytecodeVM->setScheduler(scheduler);

  
  // When a hotkey action Fiber is executed (function_id == 0xFFFFFFFF),
  // ExecutionEngine will call this callback to invoke the C++ hotkey action
  executionEngine->setHotkeyActionCallback([](uint32_t fiber_id) {
    
    auto* callback = HotkeyActionWrapper::getCallback(fiber_id);
    if (callback && *callback) {
      try {
        
        // when scheduling the action Fiber. Just execute the callback.
        (*callback)();  // Execute the C++ hotkey action function
      } catch (const std::exception& e) {
        error("Exception in hotkey action (Fiber " + std::to_string(fiber_id) + "): " + e.what());
      }
    } else {
      warn("No callback registered for hotkey action Fiber " + std::to_string(fiber_id));
    }
    // Note: Callback unregistration happens when Fiber is cleaned up
  });

  
  if (hotkeyManager) {
    hotkeyManager->getConditionalHotkeyManager().setExecutionEngine(executionEngine.get());
    hotkeyManager->getConditionalHotkeyManager().setEventQueue(eventQueue);
    hotkeyManager->getConditionalHotkeyManager().registerVarChangedHandler();
    
    
    hotkeyManager->getConditionalHotkeyManager().setScheduler(scheduler);
    
    
    conditionCompiler = new HotkeyConditionCompiler();
    hotkeyManager->getConditionalHotkeyManager().setConditionCompiler(conditionCompiler);
    hotkeyManager->getConditionalHotkeyManager().setBytecodeVM(bytecodeVM.get());
    
    
    // HotkeyActionWrapper uses static methods and doesn't need to be stored
    
    
    // HotkeyActionContext::clearContext() and HotkeyActionStateSync::clearAll() are called
    // from HotkeyActionContext.cpp static initialization if needed
    debug("Reactive hotkey system initialized");
    
    auto modeManager = hotkeyManager->getModeManager();
    if (modeManager) {
      modeManager->setExecutionEngine(executionEngine.get());
      modeManager->setEventQueue(eventQueue);
      modeManager->registerVarChangedHandler();
    }
  }

  // Set HostBridge pointer on EventListener for timer checking
  if (io && io->GetEventListener()) {
  io->GetEventListener()->setHostBridge(hostBridge.get());
  if (executionEngine) {
    io->GetEventListener()->setExecutionEngine(executionEngine.get());
    debug("ExecutionEngine integrated into EventListener main loop");
  }
}
#else
  info("Havel language disabled");
#endif
  if (WindowManagerDetector::IsX11()) {
    Display *display = DisplayManager::GetDisplay();
    if (!display) {
      throw std::runtime_error("Failed to open X11 display");
    }
  }
  if(Configs::Get().Get<bool>("Debug.AutoExit", false)){
    std::thread([this]() {
      auto s = Configs::Get().Get<int>("Debug.AutoExitDelay", 15);
      std::this_thread::sleep_for(std::chrono::seconds(s));
      debug("AutoExit enabled - exiting after {} seconds", s);
      std::exit(0);
    }).detach();
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

  
  if (conditionCompiler) {
    debug("Havel::cleanup() - deleting HotkeyConditionCompiler");
    delete conditionCompiler;
    conditionCompiler = nullptr;
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

  // Release ImageService handles (prevents leak when scripts don't call image.release)
  auto imgSvc = havel::host::ServiceRegistry::instance().get<havel::host::ImageService>();
  if (imgSvc) {
    imgSvc->releaseAll();
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

  debug("Signal handling initialized - fallback handlers for REPL mode");
  } else {
  debug("Signal handling initialized - EventListener manages signals");
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
