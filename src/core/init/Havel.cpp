#include "core/init/Havel.hpp"
#include <cstdio>
#include "havel-lang/runtime/Modules.hpp"
#include "havel-lang/runtime/concurrency/Scheduler.hpp"
#include "havel-lang/runtime/concurrency/Fiber.hpp"
#include "havel-lang/runtime/concurrency/DependencyTracker.hpp"
#include "havel-lang/runtime/execution/ExecutionEngine.hpp"
#include "core/hotkey/HotkeyActionWrapper.hpp"
#include "extensions/gui/automation_suite/AutomationSuite.hpp"
#include "core/brightness/BrightnessManager.hpp"
#include "core/config/ConfigManager.hpp"
#include "core/io/IO.hpp"
#include "utils/ExitHandler.hpp"
#include "core/hotkey/HotkeyManager.hpp"
#include "core/window/WindowManager.hpp"
#include "core/automation/AutomationManager.hpp"
#include "modules/HostModules.hpp"
#include "utils/StartupTiming.hpp"
#ifdef HAVEL_ENABLE_LLVM
#include "havel-lang/compiler/BytecodeOrcJIT.h"
#endif
#include "core/display/DisplayManager.hpp"
#include "core/mode/ModeManager.hpp"
#include "core/media/MPVController.hpp"
#include "core/media/AudioManager.hpp"
#include "core/io/EventListener.hpp"
#include "core/io/KeyTap.hpp"
#include "utils/Logger.hpp"
#include "utils/DebugFlags.hpp"
#include "core/util/Env.hpp"
#include "core/net/NetworkManager.hpp"
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
  if (debugging::debug_io) debug("Havel destroyed");
}

void Havel::initialize(bool isStartup) {
    fprintf(stderr, "[HAVEL-INIT] initialize() called\n");
    if (debugging::debug_io) debug("Initializing HvC components...");
    if (debugging::debug_io) debug("isStartup: " + std::to_string(isStartup));
    if (debugging::debug_io) debug("GUI: " + std::to_string(guiMode));

    auto t0 = havel::startup_now();

    // Initialize in dependency order
    io = std::make_shared<IO>();
    if (replMode) {
      io->SetEventListenerThreaded(false);
    }
    auto t = havel::startup_now();
    havel::startup_timing_report("IO-create", t0);
  if (!io) {
    throw std::runtime_error("Failed to create IO manager");
  }

    // Create HotkeyManager (depends on IO)
    hotkeyManager = std::make_shared<HotkeyManager>(io);
    havel::startup_timing_report("HotkeyManager-create", t);
    t = havel::startup_now();
    if (!hotkeyManager) {
        throw std::runtime_error("Failed to create HotkeyManager");
    }

    windowManager = std::make_shared<WindowManager>();
    havel::startup_timing_report("WindowManager-create", t);
    t = havel::startup_now();



    mpv = std::make_shared<MPVController>();
    mpv->Initialize();
    havel::startup_timing_report("MPVController-init", t);
    t = havel::startup_now();

    audioManager = std::make_shared<AudioManager>(AudioBackend::AUTO);
    havel::startup_timing_report("AudioManager-init", t);
    t = havel::startup_now();

    brightnessManager = std::make_shared<BrightnessManager>();
    brightnessManager->init();
    havel::startup_timing_report("BrightnessManager-init", t);
    t = havel::startup_now();

    automationManager = std::make_shared<automation::AutomationManager>(io);
    havel::startup_timing_report("AutomationManager-create", t);
    t = havel::startup_now();

    // Initialize NetworkManager (singleton)
    networkManager = std::shared_ptr<net::NetworkManager>(
        &net::NetworkManager::getInstance(), [](net::NetworkManager *) {});
    havel::startup_timing_report("NetworkManager-init", t);
    t = havel::startup_now();

#ifdef ENABLE_HAVEL_LANG
  if (debugging::debug_io) debug("Initializing bytecode VM and HostBridge...");

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
        havel::startup_timing_report("VM-create", t);
    t = havel::startup_now();

#ifdef HAVEL_ENABLE_LLVM
  // Initialize JIT if enabled
  bool useJIT = Configs::Get().Get<bool>("Compiler.JIT", true);
  // Override with CLI config if present (assuming this->commandLineArgs or similar)
  // For now we check the instance-level config if we had it, but Havre uses LaunchConfig in Launcher.
  // We'll trust the ConfigManager which should be updated by Launcher.
  
  if (useJIT) {
    if (debugging::debug_io) debug("JIT compilation enabled");
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


        // Create Modules
        modules_ = havel::createModules(*hostContext);
        hostContext->modules = modules_.get();
        havel::startup_timing_report("Modules-create", t);
        t = havel::startup_now();

        // Register stdlib + install
        modules_->install();
        havel::startup_timing_report("Modules-install", t);
        t = havel::startup_now();
        for (const auto& [name, fn] : modules_->options().host_functions) {
            bytecodeVM->registerHostFunction(name, fn);
        }
        havel::startup_timing_report("host-functions-register", t);
        t = havel::startup_now();

{
    std::string stdlibPath;
    const char* envStdlib = std::getenv("HAVEL_STDLIB");
    if (envStdlib && envStdlib[0] != '\0') {
        stdlibPath = envStdlib;
    } else {
        auto exePath = Env::executable();
        if (!exePath.empty()) {
        stdlibPath = (std::filesystem::path(exePath).parent_path() / ".." / "modules" / "std").string();
            } else {
                stdlibPath = "./modules/std";
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
      auto canonicalRoot = std::filesystem::canonical(modulesPath).string();
      bytecodeVM->moduleLoader().addSearchPath(canonicalRoot + "/lang");
      bytecodeVM->moduleLoader().addSearchPath(canonicalRoot + "/std");
      bytecodeVM->moduleLoader().addSearchPath(canonicalRoot + "/app");
      bytecodeVM->moduleLoader().addSearchPath(canonicalRoot);

      // Add module .so paths for havel_mod_<name>.so discovery
                auto modulesDir = std::filesystem::path(exePath).parent_path() / ".." / "modules";
      if (std::filesystem::exists(modulesDir)) {
        bytecodeVM->moduleLoader().addModuleSoPath(
          std::filesystem::canonical(modulesDir).string());
      }
            }
        }
    }


  scheduler = &compiler::Scheduler::instance();
  if (!scheduler) {
    throw std::runtime_error("Failed to create Scheduler");
  }

        // EventQueue is created by ConcurrencyBridge during modules_->install()
        compiler::EventQueue* eventQueue = hostContext->eventQueue;
        if (!eventQueue) {
            throw std::runtime_error("Failed to get EventQueue from Modules");
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

	{
		auto* eventQueue = hostContext->eventQueue;
		auto* vm = bytecodeVM.get();
		auto* watcherRegistry = executionEngine->getWatcherRegistry();
		if (eventQueue && vm && watcherRegistry) {
			eventQueue->onEvent(compiler::EventType::VAR_CHANGED,
				[vm, watcherRegistry, hostCtx = hostContext.get()](const compiler::Event& event) {
				auto* name_ptr = static_cast<std::string*>(event.ptr);
				if (!name_ptr) return;
				std::string var_name = std::move(*name_ptr);
				delete name_ptr;
				auto fired = watcherRegistry->onVariableChanged(
					var_name, [vm, watcherRegistry](uint32_t wid) -> bool {
					const auto* w = watcherRegistry->getWatcher(wid);
					if (!w) return false;
					const compiler::BytecodeChunk* saved_chunk = nullptr;
					bool set_chunk = false;
					if (w->condition_chunk) {
						saved_chunk = vm->getCurrentChunk();
						vm->setCurrentChunkPublic(w->condition_chunk);
						set_chunk = true;
					}
					auto tracker = std::make_shared<compiler::DependencyTracker>();
					compiler::DependencyTrackerScope scope(tracker);
					bool result = vm->evaluateConditionBytecode(w->condition_func_id, w->condition_ip);
					if (set_chunk) vm->setCurrentChunkPublic(saved_chunk);
					return result;
				});
				for (auto* fiber : fired) {
					if (fiber) {
						try {
							compiler::Value body_func = compiler::Value::makeFunctionObjId(fiber->current_function_id);
							vm->call(body_func, {});
						} catch (...) {}
					}
				}
				vm->processSignalBindings(var_name);
				auto* sched = vm->getScheduler();
				if (sched) {
				sched->forEachConditionalHotkey(
					[vm, hostCtx, &var_name, sched](compiler::Scheduler::Goroutine* g) {
					if (!g) return;
					if (g->state != compiler::Scheduler::GoroutineState::Suspended ||
						g->suspension_reason.load(std::memory_order_acquire) != compiler::Scheduler::SuspensionReason::HotkeyWait) return;
					if (g->hotkey_condition_deps.empty() ||
						g->hotkey_condition_deps.count(var_name) == 0) return;
					auto condVal = vm->externalRootValue(g->hotkey_condition_callback_id);
					if (!condVal) return;
						auto tracker = std::make_shared<compiler::DependencyTracker>();
						compiler::DependencyTrackerScope scope(tracker);
						bool conditionMet = false;
						try {
							compiler::Value result = vm->callFunctionSync(*condVal, {});
							conditionMet = vm->toBool(result);
						} catch (...) {}
						auto newDeps = tracker->getGlobalDependencies();
						auto fieldDeps = tracker->getFieldDependencies();
						newDeps.insert(fieldDeps.begin(), fieldDeps.end());
						g->hotkey_condition_deps = std::move(newDeps);
						bool prev = g->hotkey_condition_last_result;
						g->hotkey_condition_last_result = conditionMet;
					if (prev == conditionMet) return;
					if (conditionMet) {
						if (!g->hotkey_condition_alias.empty()) {
							auto* hm = hostCtx ? hostCtx->hotkeyManager : nullptr;
							if (hm) hm->SetHotkeyGrab(g->hotkey_condition_alias, true);
						}
						sched->wakeHotkey(g);
					} else {
						if (!g->hotkey_condition_alias.empty()) {
							auto* hm = hostCtx ? hostCtx->hotkeyManager : nullptr;
							if (hm) hm->SetHotkeyGrab(g->hotkey_condition_alias, false);
						}
					}
					});
				}
			});
		}
	}

 if (hotkeyManager) {
 hotkeyManager->setEventQueue(eventQueue);


 if (debugging::debug_io) debug("Reactive hotkey system initialized");
    
        auto modeManager = hotkeyManager->getModeManager();
        if (modeManager) {
            modeManager->setOnModeChanged([vm = bytecodeVM.get()](
                const std::string &newMode, const std::string &) {
                auto ref = vm->createRuntimeString(newMode);
                vm->setGlobal("mode", havel::core::Value::makeStringId(ref.id));
            });
            auto ref = bytecodeVM->createRuntimeString(modeManager->getCurrentMode());
            bytecodeVM->setGlobal("mode", havel::core::Value::makeStringId(ref.id));
        }
  }

  // Set HostBridge pointer on EventListener for timer checking
  fprintf(stderr, "[HAVEL-INIT] about to set EE on EventListener: io=%p el=%p ee=%p\n",
          (void*)io.get(), io ? (void*)io->GetEventListener() : nullptr, (void*)executionEngine.get());
  if (io && io->GetEventListener()) {
            io->GetEventListener()->setModules(modules_.get());
            io->GetEventListener()->setDeferredSendFlush([ioPtr = io.get()](){
                ioPtr->FlushPendingSends();
            });
  if (executionEngine) {
    io->GetEventListener()->setExecutionEngine(executionEngine.get());
    if (debugging::debug_io) debug("ExecutionEngine integrated into EventListener main loop");
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
havel::registerExitCleanup([this]() { performCleanup(); });
havel::startup_timing_report("Havel::initialize TOTAL", t0);
if(Configs::Get().Get<bool>("Debug.AutoExit", false)){
		std::thread([this]() {
			auto s = Configs::Get().Get<int>("Debug.AutoExitDelay", 15);
			std::this_thread::sleep_for(std::chrono::seconds(s));
			if(!Configs::Get().Get<bool>("Debug.AutoExit", false)){
				return;
			}
			if (debugging::debug_io) debug("AutoExit enabled - exiting after {} seconds", s);
			this->exit();
		}).detach();
	}
}

void Havel::cleanup() noexcept {
  try {
  std::call_once(cleanupOnce, [this]() {
  if (debugging::debug_io) debug("Havel::cleanup() - starting cleanup");

  // Force save config before everything else
  try {
    Configs::Get().ForceSave();
    if (debugging::debug_io) debug("Havel::cleanup() - config saved");
  } catch (...) {}

  // Stop timer first
  stopPeriodicTimer();

  // Stop EventListener FIRST
  if (io && io->GetEventListener()) {
    if (debugging::debug_io) debug("Havel::cleanup() - stopping EventListener");
    io->GetEventListener()->Stop();
  }

  // Destroy hotkeyManager
  if (hotkeyManager) {
    if (debugging::debug_io) debug("Havel::cleanup() - destroying HotkeyManager");
    hotkeyManager->cleanup();
    hotkeyManager.reset();
  }



 // Stop ExecutionEngine and Scheduler BEFORE destroying VM
 if (executionEngine) {
 if (debugging::debug_io) debug("Havel::cleanup() - shutting down ExecutionEngine");
 executionEngine->shutdown();
 executionEngine.reset();
 }
 auto& scheduler = compiler::Scheduler::instance();
 if (scheduler.isRunning()) {
 if (debugging::debug_io) debug("Havel::cleanup() - stopping Scheduler");
 scheduler.stop();
 }

 // Destroy VM
 if (bytecodeVM) {
 if (debugging::debug_io) debug("Havel::cleanup() - destroying VM");
 bytecodeVM.reset();
 }

        // Destroy Modules
        if (modules_) {
            if (debugging::debug_io) debug("Havel::cleanup() - destroying Modules");
            modules_->shutdown();
            modules_.reset();
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
  // Note: This runs in main binary, so ServiceRegistry singleton is correct here.
  if (imgSvc) {
    imgSvc->releaseAll();
  }

  // Destroy IO LAST
  if (io) {
    io->cleanup();
    io.reset();
  }

  DisplayManager::Close();

  if (debugging::debug_io) debug("Havel::cleanup() - cleanup complete");
  }); // std::call_once
  } catch (...) {}
}

void Havel::gracefulExit(int code, bool fromSignal) {
  havel::exit(fromSignal ? ExitReason::SignalInt : ExitReason::Normal, code);
}

void Havel::performCleanup() {
  if (shutdownRequested) {
    return;
  }
  shutdownRequested = true;
  info("Exit requested - starting graceful shutdown");

  if (io && io->GetEventListener()) {
    info("Stopping EventListener before exit...");
    io->GetEventListener()->Stop();
    info("EventListener stopped");
  }

  cleanup();
}

void Havel::exit() {
  performCleanup();
  havel::exit(ExitReason::Normal, 0);
}

void Havel::setShutdownCallback(std::function<void()> cb) {
  if (io && io->GetEventListener()) {
    io->GetEventListener()->SetShutdownCallback(std::move(cb));
  }
}

void Havel::setupSignalHandling() {
  try {
    // Set up fallback signal handlers for REPL mode
    if (!guiMode) {
      struct sigaction sa;
      sa.sa_flags = 0;
      sigemptyset(&sa.sa_mask);
      sa.sa_handler = [](int sig) {
        int code = (sig == SIGINT || sig == SIGTERM || sig == SIGQUIT) ? 0 : sig + 128;
        bool crash = (sig == SIGSEGV || sig == SIGABRT || sig == SIGBUS || sig == SIGILL || sig == SIGFPE);
        havel::exit(crash ? ExitReason::SignalCrash
                     : sig == SIGINT  ? ExitReason::SignalInt
                     : sig == SIGTERM ? ExitReason::SignalTerm
                     : sig == SIGQUIT ? ExitReason::SignalQuit
                     : ExitReason::Forced,
                    code);
      };
      sigaction(SIGINT, &sa, nullptr);
      sigaction(SIGTERM, &sa, nullptr);
      sigaction(SIGABRT, &sa, nullptr);
      sigaction(SIGSEGV, &sa, nullptr);
      sigaction(SIGQUIT, &sa, nullptr);

  if (debugging::debug_io) debug("Signal handling initialized - fallback handlers for REPL mode");
  } else {
  if (debugging::debug_io) debug("Signal handling initialized - EventListener manages signals");
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

      // Window check (only if timer is still running)
      if (timerRunning &&
          std::chrono::duration_cast<std::chrono::milliseconds>(now - lastWindowCheck)
              .count() >= WINDOW_CHECK_INTERVAL_MS) {
        if (timerRunning && hotkeyManager) {
        }
        lastWindowCheck = now;
      }

      // Config check
      if (std::chrono::duration_cast<std::chrono::seconds>(now - lastCheck)
              .count() >= CONFIG_CHECK_INTERVAL_S) {
        lastCheck = now;
      }

      // Wait for interval or stop signal
      std::unique_lock<std::mutex> lock(timerMutex);
      timerCv.wait_for(lock, std::chrono::milliseconds(PERIODIC_INTERVAL_MS),
                       [this]() { return !timerRunning.load(); });
    }
  });
}

void Havel::stopPeriodicTimer() {
  timerRunning = false;
  timerCv.notify_one();
  if (timerThread) {
    std::this_thread::sleep_for(std::chrono::milliseconds(PERIODIC_INTERVAL_MS + 50));
    if (timerThread->joinable()) {
      timerThread->join();
    } else {
      warn("Periodic timer thread already exited");
    }
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
