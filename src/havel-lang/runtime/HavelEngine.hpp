#pragma once

#include "../compiler/vm/VM.hpp"
#include "Modules.hpp"
#include "../compiler/runtime/EventQueue.hpp"
#include "../compiler/core/Pipeline.hpp"
#ifdef HAVEL_ENABLE_LLVM
#include "../compiler/BytecodeOrcJIT.h"
#endif
#include "HostAPI.hpp"
#include "../../modules/HostModules.hpp"
#ifndef ENABLE_MODULE_PLUGINS
#include "../../modules/ffi/FFIModule.hpp"
#endif
#include "../stdlib/BytecodeBuilderModule.hpp"
#include "../../host/ServiceRegistry.hpp"
#include "../../core/util/Env.hpp"
#include "../runtime/concurrency/WatcherRegistry.hpp"
#include "../runtime/concurrency/Scheduler.hpp"
#include "../runtime/concurrency/Fiber.hpp"
#include "core/config/ConfigManager.hpp"
#include "core/io/IO.hpp"
#include "core/brightness/BrightnessManager.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <cstdlib>
#include "utils/StartupTiming.hpp"

namespace havel {

struct EngineConfig {
    bool debugBytecode = false;
    bool debugLexer = false;
    bool debugParser = false;
    bool debugAst = false;
    bool stopOnError = false;
    bool leanMinimalStartup = false;
  bool pureStdlib = false;
  compiler::VMConfig vmConfig;
  host::ServiceFilter serviceIncludes;
  host::ServiceFilter serviceExcludes;
};

class HavelEngine {
public:
    explicit HavelEngine(const EngineConfig& config = {})
        : config_(config) {}

    ~HavelEngine() { shutdown(); }

    HavelEngine(const HavelEngine&) = delete;
    HavelEngine& operator=(const HavelEngine&) = delete;

    void initializeMinimal() {
        io_holder_ = std::make_shared<IO>();
        brightnessManager_ = std::make_shared<BrightnessManager>();
        brightnessManager_->init();
        auto hostAPI = std::make_shared<HostAPI>(io_holder_.get(), nullptr, Configs::Get(), nullptr, brightnessManager_.get());
        initializeFull(hostAPI, config_.leanMinimalStartup);
    }

    void initializeFull(std::shared_ptr<IHostAPI> hostAPI, bool leanStartup = false) {
        if (initialized_) return;
        auto t0 = havel::startup_now();

        host::ServiceRegistry::instance().clear();
        initializeServiceRegistry(hostAPI, config_.serviceIncludes, config_.serviceExcludes);
        auto t = havel::startup_now();
        havel::startup_timing_report("service-registry", t0);

        hostContext_ = std::make_unique<HostContext>(createHostContext(hostAPI));
        havel::startup_timing_report("host-context", t);
        t = havel::startup_now();

        vm_ = std::make_shared<compiler::VM>(*hostContext_, config_.vmConfig);
        hostContext_->vm = vm_.get();
        havel::startup_timing_report("vm-create", t);
        t = havel::startup_now();

        // Set up scheduler for goroutine/thread support
        vm_->setScheduler(&compiler::Scheduler::instance());

        // Apply VMConfig scheduler settings
        compiler::Scheduler::instance().setDefaultTickInstructions(
            config_.vmConfig.goroutine_tick_instructions,
            config_.vmConfig.goroutine_hotkey_tick_instructions);

#ifdef HAVEL_ENABLE_LLVM
if (Configs::Get().Get<bool>("Compiler.JIT", true)) {
jitCompiler_ = std::make_unique<compiler::BytecodeOrcJIT>();
jitCompiler_->setDebugMode(Configs::Get().Get<bool>("Compiler.DebugJIT", false));
jitCompiler_->setDumpAsmToFile(Configs::Get().Get<bool>("Compiler.OutputAsm", false));
jitCompiler_->setDumpIR(Configs::Get().Get<bool>("Compiler.DumpIR", false));
jitCompiler_->setShowWarnings(Configs::Get().Get<bool>("Compiler.JITWarnings", true));
vm_->setHotFunctionCallback([jit_ptr = jitCompiler_.get()](const compiler::BytecodeFunction& func) {
if (!jit_ptr->isCompiled(func.name)) {
jit_ptr->compileFunction(func);
}
});
vm_->setJITCompiler(jitCompiler_.get());
}
#endif

        modules_ = havel::createModules(*hostContext_);
        hostContext_->modules = modules_.get();
        havel::startup_timing_report("modules-create", t);
        t = havel::startup_now();

        // Set stdlib path BEFORE registration so pure-Havel stdlib modules
        // (type.hv, etc.) can be found by loadModule during init.
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
            vm_->moduleLoader().setStdlibPath(stdlibPath);
        }

        // Add module search paths so `use "lexer"` etc. resolve at runtime
        {
            auto exePath = Env::executable();
            std::string modulesRoot;
            if (!exePath.empty()) {
                modulesRoot = (std::filesystem::path(exePath).parent_path() / ".." / "modules").string();
            } else {
                modulesRoot = "./modules";
            }
            auto canonicalRoot = std::filesystem::exists(modulesRoot)
                ? std::filesystem::canonical(modulesRoot).string() : modulesRoot;
            vm_->moduleLoader().addSearchPath(canonicalRoot + "/lang");
            vm_->moduleLoader().addSearchPath(canonicalRoot + "/std");
            vm_->moduleLoader().addSearchPath(canonicalRoot + "/app");
            vm_->moduleLoader().addSearchPath(canonicalRoot);
        }

  vm_->suspendGC();
  if (leanStartup) {
    if (config_.pureStdlib) {
      havel::registerPureStdLib(*vm_);
      havel::startup_timing_report("stdlib-register-pure", t);
      t = havel::startup_now();
#if !defined(HAVEL_PURE_VM) && !defined(ENABLE_MODULE_PLUGINS)
      {
        compiler::VMApi ffiApi(*vm_);
        modules::ffi::registerFFIModule(ffiApi);
      }
      havel::startup_timing_report("ffi-register", t);
      t = havel::startup_now();
#endif
    } else {
      havel::registerCoreStdLib(*vm_);
      havel::startup_timing_report("stdlib-register-core", t);
      t = havel::startup_now();
    }
  }
  // Always ensure bytecodeBuilder is available for self-hosted compilation
  if (!vm_->isLazyModuleRegistered("bytecodeBuilder")) {
    vm_->registerLazyModule("bytecodeBuilder", [](compiler::VMApi &a) {
      stdlib::registerBytecodeBuilderModule(a);
    }, {"bc"});
  }
  vm_->resumeGC();
        modules_->install(
            leanStartup ? havel::InstallProfile::Core
                        : havel::InstallProfile::Full,
            !leanStartup);
        havel::startup_timing_report("modules-install", t);
        t = havel::startup_now();

        for (const auto& [name, fn] : modules_->options().host_functions) {
            vm_->registerHostFunction(name, fn);
        }
        havel::startup_timing_report("host-functions-register", t);
        t = havel::startup_now();

        vm_->setTimerCheckFunction([this]() { modules_->checkTimers(); });

if (hostContext_->eventQueue) {
vm_->setEventQueue(hostContext_->eventQueue);
hostContext_->eventQueue->onEvent(compiler::EventType::TIMER_FIRE,
[this](const compiler::Event& event) {
auto *payload = static_cast<std::pair<compiler::Value, uint32_t>*>(event.ptr);
if (!payload) return;
compiler::Value closure = payload->first;
uint32_t timer_id = payload->second;
bool is_timeout = (event.data1 == 1);
delete payload;
if (!vm_) return;
try {
compiler::Value result = vm_->callFunction(closure, {});
if (is_timeout) {
vm_->addTimeoutResult(timer_id, result);
} else {
vm_->addIntervalResult(timer_id, result);
}
} catch (const std::exception& e) {
::havel::error("[HavelEngine] Timer callback exception: {}", e.what());
}
});
}

        // Wire watcher registry for reactive when blocks
        watcher_registry_ = std::make_unique<compiler::WatcherRegistry>();
        vm_->setWatcherRegistry(watcher_registry_.get());
        havel::startup_timing_report("watcher-registry", t);

        havel::startup_timing_report("HavelEngine::initializeFull TOTAL", t0);
        initialized_ = true;
    }

    compiler::Value execute(const std::string& source,
                            const std::string& entryPoint = "__main__",
                            const std::string& compileUnitName = "unit") {
        if (!initialized_) {
            throw std::runtime_error("HavelEngine not initialized");
        }

        compiler::PipelineOptions options = modules_->options();
        options.compile_unit_name = compileUnitName;
        options.vm_override = vm_.get();
        options.debugBytecode = config_.debugBytecode;
        if (config_.vmConfig.max_instructions > 0 && options.max_instructions == 0) {
            options.max_instructions = config_.vmConfig.max_instructions;
        }

        auto result = compiler::runBytecodePipeline(source, entryPoint, options);

        
        // Process pending scheduler goroutines after main script completes
        processGoroutines();

        return result.return_value;
    }

    compiler::VM* vm() const { return vm_.get(); }
    Modules* modules() const { return modules_.get(); }
    bool isInitialized() const { return initialized_; }

    void shutdown() {
        if (!initialized_) return;
        if (modules_) {
            modules_->shutdown();
        }
        if (vm_) {
            vm_->setJITCompiler(nullptr);
        }
#ifdef HAVEL_ENABLE_LLVM
        jitCompiler_.reset();
#endif
        vm_.reset();
        modules_.reset();
        hostContext_.reset();
        initialized_ = false;
    }

private:
	EngineConfig config_;
    std::shared_ptr<compiler::VM> vm_;
    std::shared_ptr<IO> io_holder_;
    std::shared_ptr<BrightnessManager> brightnessManager_;
#ifdef HAVEL_ENABLE_LLVM
    std::unique_ptr<compiler::BytecodeOrcJIT> jitCompiler_;
#endif
    std::unique_ptr<HostContext> hostContext_;
    std::shared_ptr<Modules> modules_;
    std::unique_ptr<compiler::WatcherRegistry> watcher_registry_;
    bool initialized_ = false;

  static compiler::Scheduler::SuspensionReason toSchedulerReasonPublic(uint8_t fiberReason) {
    using F = compiler::SuspensionReason;
    using S = compiler::Scheduler::SuspensionReason;
    switch (static_cast<F>(fiberReason)) {
    case F::NONE: return S::None;
    case F::YIELD: return S::None;
    case F::CHANNEL_RECV: return S::ChannelWait;
    case F::CHANNEL_SEND: return S::ChannelSendWait;
    case F::THREAD_JOIN: return S::ThreadWait;
    case F::TIMER: return S::TimerWait;
    case F::SLEEP: return S::SleepWait;
    case F::EXTERNAL: return S::None;
    case F::HOTKEY_WAIT: return S::HotkeyWait;
    case F::AWAIT: return S::None;
    case F::COROUTINE_WAIT: return S::CoroutineWait;
    default: return S::None;
    }
  }

  void processGoroutines() {
    auto* sched = vm_->getScheduler();
    if (!sched) return;

    // Ensure current_chunk is set for plain functions (not closures)
    auto mainChunk = vm_->getMainChunk();
    if (mainChunk) {
      vm_->setCurrentChunkPublic(mainChunk.get());
    }

    bool anyExecuted = false;
    int idleCycles = 0;
    do {
      anyExecuted = false;
      sched->wakeSleepingGoroutines();
      auto* g = sched->pickNext();
      if (!g) {
        if (sched->suspendedCount() > 0) {
          if (++idleCycles >= 100) break;
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
          continue;
        }
        break;
      }
      idleCycles = 0;

      // Set as current goroutine so VM opcodes can access it via scheduler_->current()
      sched->setCurrent(g);

      // Start and run this goroutine to completion
      // pickNext() returns goroutines with Runnable or Created state (does NOT change state).
      if (g->state == compiler::Scheduler::GoroutineState::Created) {
        bool ok = vm_->startGoroutineCall(g->function_id, g->closure_id, g->locals);
        if (ok) {
          g->state = compiler::Scheduler::GoroutineState::Runnable;
          vm_->runDispatchLoopPublic(0);
          anyExecuted = true;
        }
      } else if (g->state == compiler::Scheduler::GoroutineState::Runnable ||
                 g->state == compiler::Scheduler::GoroutineState::Running) {
        // Resumed goroutine (unparked from await/sleep)
        if (g->fiber) {
          vm_->loadFiberStatePublic(g->fiber);
          // Replace placeholder null with actual resume_value
          if (g->wait_handle.type != compiler::Scheduler::AwaitableType::NONE &&
              g->wait_handle.type != compiler::Scheduler::AwaitableType::SLEEP) {
            vm_->replaceStackTop(g->wait_handle.resume_value);
            g->wait_handle.clear();
          }
        }
        vm_->runDispatchLoopPublic(0);
        anyExecuted = true;
      }

      // Check if the goroutine suspended (await/sleep) or finished
      uint8_t lastReason = vm_->getLastSuspensionReason();
      void* lastContext = vm_->getLastSuspensionContext();
      if (lastReason != 0) {
        // Goroutine suspended — save fiber state and mark as Suspended
        vm_->clearLastSuspension();
        if (g->fiber) {
          vm_->saveFiberStatePublic(g->fiber);
        }
        auto schedReason = toSchedulerReasonPublic(lastReason);
        g->state = compiler::Scheduler::GoroutineState::Suspended;
        g->suspension_reason = schedReason;
        if (g->fiber) {
          g->fiber->state = compiler::FiberState::SUSPENDED;
          g->fiber->suspended_reason = static_cast<compiler::SuspensionReason>(lastReason);
        }
        // For SLEEP, set the deadline on the wait_handle
        if (static_cast<compiler::SuspensionReason>(lastReason) == compiler::SuspensionReason::SLEEP) {
          auto ms = reinterpret_cast<intptr_t>(lastContext);
          g->wait_handle.type = compiler::Scheduler::AwaitableType::SLEEP;
          g->wait_handle.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        }
        if (sched->current() == g) {
          sched->clearCurrent();
        }
      } else if (g->persistent) {
        // Persistent goroutines (hotkey system): re-suspend instead of Done.
        g->state = compiler::Scheduler::GoroutineState::Suspended;
        g->suspension_reason = compiler::Scheduler::SuspensionReason::HotkeyWait;
        if (g->fiber) {
          g->fiber->state = compiler::FiberState::SUSPENDED;
          g->fiber->suspended_reason = compiler::SuspensionReason::HOTKEY_WAIT;
        }
        if (sched->current() == g) {
          sched->clearCurrent();
        }
      } else {
        g->state = compiler::Scheduler::GoroutineState::Done;
        if (g->fiber) {
          g->fiber->state = compiler::FiberState::DONE;
        }
      }
    } while (anyExecuted);
  }
};

} // namespace havel
