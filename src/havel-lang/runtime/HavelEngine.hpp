#pragma once

#include "../core/Value.hpp"
#include "compiler/vm/VM.hpp"
#include "Modules.hpp"
#include "../compiler/runtime/EventQueue.hpp"
#include "../compiler/core/Pipeline.hpp"
#include "../compiler/core/BytecodeIR.hpp"
#include "../compiler/runtime/RuntimeSupport.hpp"
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
#include "../../utils/InstallPaths.hpp"
#include "../runtime/concurrency/WatcherRegistry.hpp"
#include "../runtime/concurrency/Scheduler.hpp"
#include "../runtime/concurrency/Fiber.hpp"
#include "../runtime/concurrency/DependencyTracker.hpp"
#include "core/config/ConfigManager.hpp"
#include "core/io/IO.hpp"
#include "core/hotkey/HotkeyManager.hpp"
#include "../stdlib/HotkeyModule.hpp"
#include "core/window/WindowManager.hpp"
#include "core/BrightnessManager.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <cstdlib>
#include <mutex>
#include <unordered_set>
#include <vector>
#include "utils/StartupTiming.hpp"

namespace havel {

struct EngineConfig {
    bool debugBytecode = false;
    bool debugLexer = false;
    bool debugParser = false;
    bool debugAst = false;
    bool debugEmitter = false;
    bool traceExecution = false;
    bool optimizeBytecode = false;  // run the CFG optimization pipeline
    bool stopOnError = false;
    bool leanMinimalStartup = false;
    bool headlessMode = false;
    bool pureStdlib = false;
    std::string self_hosted_modules_path;
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
        hotkeyManager_ = std::make_shared<HotkeyManager>(io_holder_);
        windowManager_ = std::make_shared<WindowManager>();
        if (!config_.headlessMode) {
            brightnessManager_ = std::make_shared<BrightnessManager>();
        }
        auto hostAPI = std::make_shared<HostAPI>(io_holder_.get(), hotkeyManager_.get(), Configs::Get(), windowManager_.get(), nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, config_.headlessMode ? nullptr : brightnessManager_.get());
        initializeFull(hostAPI, config_.leanMinimalStartup);
    }

    void initializeFull(std::shared_ptr<IHostAPI> hostAPI, bool leanStartup = false) {
        if (initialized_) return;
        auto t0 = havel::startup_now();

        host::ServiceRegistry::instance().clear();
        initializeServiceRegistry(hostAPI, config_.serviceIncludes, config_.serviceExcludes, config_.headlessMode);
        auto t = havel::startup_now();
        havel::startup_timing_report("service-registry", t0);

        hostContext_ = std::make_unique<HostContext>(createHostContext(hostAPI));
        havel::startup_timing_report("host-context", t);
        t = havel::startup_now();

vm_ = std::make_shared<compiler::VM>(*hostContext_, config_.vmConfig);
        if (!config_.self_hosted_modules_path.empty()) {
            vm_->setSelfHostedModulesPath(config_.self_hosted_modules_path);
        }
        vm_->setHeadlessMode(config_.headlessMode);
        if (config_.traceExecution) {
            vm_->setTraceExecution(true);
        }
        hostContext_->vm = vm_.get();
        hostAPI->SetVM(vm_.get());
        vm_->registerDefaultHostGlobals();  // Ensure host functions are in globals
  // Register traceBytecode as a host function so it's always available for self-hosted compiler
  vm_->registerHostFunction("traceBytecode", [](const std::vector<havel::core::Value> &args) -> havel::core::Value {
    if (args.empty()) return havel::core::Value::makeNull();
    std::string msg = args[0].toString();
    ::havel::debug("[traceBytecode] " + msg);
    return havel::core::Value::makeNull();
  });
        havel::startup_timing_report("vm-create", t);
        t = havel::startup_now();

        // Set up scheduler for goroutine/thread support
        vm_->setScheduler(&compiler::Scheduler::instance());

        // Set up yield callback so goroutines run during main script execution
        vm_->setYieldCallback([this]() { processGoroutinesInline(); });

        // Apply VMConfig scheduler settings
        compiler::Scheduler::instance().setDefaultTickInstructions(
            config_.vmConfig.goroutine_tick_instructions,
            config_.vmConfig.goroutine_hotkey_tick_instructions);

#ifdef HAVEL_ENABLE_LLVM
        const bool wantJIT = config_.vmConfig.tiering_enabled ||
                             Configs::Get().Get<bool>("Compiler.JIT", true);
        if (wantJIT) {
            vm_->setHotFunctionCallback([](const compiler::BytecodeFunction& func) {
                // JIT compilation will be handled by the VM's tiering system
            });
            auto* existingJIT = vm_->getJITCompiler();
            if (!existingJIT) {
                jitCompiler_ = std::make_unique<compiler::BytecodeOrcJIT>();
                existingJIT = jitCompiler_.get();
                vm_->setJITCompiler(std::move(jitCompiler_));
            }
            vm_->setHotTraceCallback([existingJIT](const compiler::BytecodeFunction& func,
                                                 uint32_t start_ip,
                                                 uint64_t hot_count) {
                if (existingJIT) {
                    existingJIT->compileTrace(func, start_ip, hot_count);
                }
            });
            existingJIT->setDebugMode(Configs::Get().Get<bool>("Compiler.DebugJIT", false));
            existingJIT->setDumpAsmToFile(Configs::Get().Get<bool>("Compiler.OutputAsm", false));
            existingJIT->setDumpIR(Configs::Get().Get<bool>("Compiler.DumpIR", false));
            existingJIT->setShowWarnings(Configs::Get().Get<bool>("Compiler.JITWarnings", true));
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
                stdlibPath = install_paths::stdlibRoot();
                if (stdlibPath.empty()) {
                    auto exePath = Env::executable();
                    if (!exePath.empty()) {
                        stdlibPath = (std::filesystem::path(exePath).parent_path() / ".." / "modules" / "std").string();
                    } else {
                        stdlibPath = "./modules/std";
                    }
                }
            }
            vm_->moduleLoader().setStdlibPath(stdlibPath);
        }

        // Add module search paths so `use "lexer"` etc. resolve at runtime.
        // Resolves the source-tree layout (<exe>/../modules) and, when that is
        // absent, the system-install layout (<prefix>/share/havel/modules).
        if (auto canonicalRoot = install_paths::modulesRoot(); !canonicalRoot.empty()) {
            auto rootStr = canonicalRoot.string();
            vm_->moduleLoader().addSearchPath(rootStr + "/lang");
            vm_->moduleLoader().addSearchPath(rootStr + "/std");
            vm_->moduleLoader().addSearchPath(rootStr + "/app");
            vm_->moduleLoader().addSearchPath(rootStr);
            vm_->moduleLoader().addModuleSoPath(rootStr);
        }

        // Add system bytecode cache directory for precompiled stdlib modules
        // (installed to share/havel/modules by the release build).
        if (auto bcRoot = install_paths::bytecodeRoot(); !bcRoot.empty()) {
            vm_->moduleLoader().addCacheDir(bcRoot.string());
        }

        // Add system module plugin directory for havel_mod_<name>.so plugins
        // (installed to lib/havel/modules by the release build).
        if (auto mpRoot = install_paths::modulePluginRoot(); !mpRoot.empty()) {
            vm_->moduleLoader().addModuleSoPath(mpRoot.string());
        }

        // Add build directory's modules folder for development builds
        // (e.g., build-debug/modules/havel_mod_<name>.so)
        {
            const auto exePath = Env::executable();
            if (!exePath.empty()) {
                auto siblingModulesDir = std::filesystem::path(exePath).parent_path() / "modules";
                std::error_code ec;
                if (std::filesystem::exists(siblingModulesDir, ec)) {
                    auto canonical = std::filesystem::canonical(siblingModulesDir, ec);
                    if (!ec) {
                        vm_->moduleLoader().addModuleSoPath(canonical.string());
                    }
                }
            }
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
  // Ensure debug module is available for self-hosted compiler (traceBytecode)
  try {
    vm_->loadModule("debug");
  } catch (const std::exception &e) {
    warning("Failed to load debug module: {}", e.what());
  }
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

        // Re-register channel prototype methods.
        // registerDefaultPrototypes() runs during HavelEngine::initializeFull
        // (line 81 above) BEFORE Modules::install() populates the
        // channel_new/channel_send/etc. host functions. As a result,
        // registerPrototypeMethodByName("channel","send","channel.send")
        // looks up "channel.send" in host_function_names_, fails to find
        // it, and falls back to index 0 — meaning ch.send(v) would
        // dispatch to host_function_names_[0] ("print" or "type"), not
        // to channel.send.
        //
        // After the registerHostFunction loop above, channel.send/
        // channel.receive/channel.close are in host_function_names_ with
        // valid indices. Re-registering here writes the correct indices
        // into prototypes_["channel"], overriding the stale idx-0 entries.
        vm_->registerPrototypeMethodByName("channel", "send", "channel.send");
        vm_->registerPrototypeMethodByName("channel", "receive", "channel.receive");
        vm_->registerPrototypeMethodByName("channel", "close", "channel.close");

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

        // Synchronous reactive when evaluation: STORE_GLOBAL commits the new
        // value then calls emitVariableChanged, which invokes this callback
        // before queueing a VAR_CHANGED event. Evaluating the watcher here
        // sees the actual post-store globals, so false->true edges fire even
        // in headless runs where the event queue is only drained after the
        // main goroutine returns (events would otherwise see a stale value).
        //
        // The conditional-hotkey re-eval arm (forEachConditionalHotkey +
        // callFunctionSync) must NOT run synchronously when emitVariableChanged
        // is itself fired from inside a goroutine's dispatch loop: doing so
        // nests a recursive VM dispatch (callFunctionSync) inside an active
        // goroutine frame, and the condition-callback's bytecode dispatch
        // crosses deepWrapModuleFunctions + FFI. This combination wedges the
        // self-hosted pipeline: the goroutine's window.active() FFI call never
        // returns, and the next tick's pickNext returns the already-running
        // fiber again, producing the apparent "hang after tick=1".
        // The fix is to queue var_names produced from inside a goroutine frame
        // and process them in processGoroutines (outside any fiber context).
        vm_->setOnVarChangedSync([this](const std::string& var_name) {
            if (!watcher_registry_ || !vm_) return;
            auto fired = watcher_registry_->onVariableChanged(
                var_name,
                [this](compiler::WatcherRegistry::WatcherId wid) -> bool {
                    const auto* w = watcher_registry_->getWatcher(wid);
                    if (!w) return false;
                    const compiler::BytecodeChunk* saved_chunk = nullptr;
                    bool set_chunk = false;
                    if (w->condition_chunk) {
                        saved_chunk = vm_->getCurrentChunk();
                        vm_->setCurrentChunkPublic(w->condition_chunk);
                        set_chunk = true;
                    }
                    auto tracker = std::make_shared<compiler::DependencyTracker>();
                    compiler::DependencyTrackerScope scope(tracker);
                    bool result = vm_->evaluateConditionBytecode(w->condition_func_id, w->condition_ip);
                    if (set_chunk) vm_->setCurrentChunkPublic(saved_chunk);
                    auto newDeps = tracker->getGlobalDependencies();
                    auto fieldDeps = tracker->getFieldDependencies();
                    newDeps.insert(fieldDeps.begin(), fieldDeps.end());
                    // Union-merge (see Havel.cpp when site): keep previously-
                    // tracked deps so short-circuited branches' globals keep
                    // triggering re-evals.
                    watcher_registry_->mergeDependencies(wid, newDeps);
                    return result;
                },
                [this](uint32_t cleanup_func_id, uint32_t) {
                    try {
                        compiler::Value cleanup_func = compiler::Value::makeFunctionObjId(cleanup_func_id);
                        vm_->call(cleanup_func, {});
                    } catch (...) {}
                });
            for (auto* fiber : fired) {
                if (!fiber) continue;
                try {
                    uint32_t prev_when_watcher = vm_->current_when_watcher_id_;
                    vm_->current_when_watcher_id_ = fiber->watcher_id;
                    compiler::Value body_func = compiler::Value::makeFunctionObjId(fiber->current_function_id);
                    vm_->call(body_func, {});
                    vm_->current_when_watcher_id_ = prev_when_watcher;
                } catch (...) {}
            }
            vm_->processSignalBindings(var_name);
            // Conditional-hotkey re-eval: defer to scheduler tick when we're
            // inside a goroutine's dispatch loop, run inline otherwise.
            // Synchronous re-eval from inside a goroutine frame nests a
            // recursive VM dispatch (callFunctionSync) inside the active
            // goroutine's frame, which corrupts FFI/VM state (e.g.
            // window.active() never returns). Drain after suspension.
            //
            // deferral must also fire when deepWrapModuleFunctions is mid-
            // frame: synchronous re-eval would cross the still-being-wrapped
            // module host wrapper FFI and wedge similarly.
            // Conditional-hotkey re-eval: defer when synchronous re-eval would
            // wedge an active goroutine dispatch frame. Two unsafe cases:
            //   1. deepWrapModuleFunctions is mid-frame: the cond callback's
            //      callFunctionSync may cross the still-being-wrapped module
            //      host wrapper FFI and wedge.
            //   2. A goroutine fiber is mid-dispatch: same risk in spirit;
            //      callFunctionSync nested inside the active dispatch may
            //      wedge the host function's FFI (e.g. window.active() never
            //      returns).
            // Defer to pending_var_changes_ for the next processGoroutines
            // tick drain.
            if (vm_->hasCurrentExecutingFiber() ||
                vm_->deep_wrap_module_functions_depth_.load() > 0) {
                std::lock_guard<std::mutex> lk(pending_var_changes_mutex_);
                pending_var_changes_.push_back(var_name);
            } else {
                reevalConditionalHotkeys(var_name);
            }
        });

        // Wire a drain into the VM so host getters (e.g. Hotkey.grab) that
        // read state derived from conditional-hotkey re-eval flush any
        // pending var changes before reading. Without this, scripts that
        // mutate a global followed by reading hotkey.grab in the same tick
        // (no yield/sleep between them) read stale grab state, since
        // pending_var_changes_ is otherwise only drained at top of
        // processGoroutines loop.
        vm_->setDrainConditionalHotkeyPendingFn([this]() {
            // Re-entrancy guard: if a re-eval is already in flight on this
            // thread (e.g. setGrab triggered by drain itself), don't recurse.
            static thread_local bool in_drain = false;
            if (in_drain) return;
            in_drain = true;
            struct Guard { bool* p; ~Guard() { *p = false; } } _g{&in_drain};
            drainPendingVarChanges();
        });

        havel::startup_timing_report("HavelEngine::initializeFull TOTAL", t0);
        initialized_ = true;
    }

    // Re-evaluate every conditional hotkey whose dep set contains var_name
    // (or whose dep set is empty: an empty set matches any change, including
    // the very first eval after registration where deps haven't been
    // recorded yet). Runs synchronously; caller must NOT be inside a
    // goroutine's dispatch loop (use pending_var_changes_ to defer instead).
    void reevalConditionalHotkeys(const std::string& var_name) {
        auto* sched = vm_ ? vm_->getScheduler() : nullptr;
        if (!sched) return;
        struct HotkeyAction {
            std::string alias;
            bool grab;
        };
        std::vector<HotkeyAction> pendingActions;
        // Re-entry guard: emit→eval→emit cycles can stack when a condition
        // callback mutates a dep variable. thread_local so concurrent
        // reeval from distinct goroutine dispatch threads don't interfere.
        static thread_local int depth = 0;
        if (depth > 0) return;
        depth++;
        struct Gd__ { ~Gd__(){ depth--; } } _gd_;
        sched->forEachConditionalHotkey(
            [this, &var_name, &pendingActions](compiler::Scheduler::Goroutine* g) {
                if (!g) return;
                if (g->state != compiler::Scheduler::GoroutineState::Suspended ||
                    g->suspension_reason.load(std::memory_order_acquire) != compiler::Scheduler::SuspensionReason::HotkeyWait) return;
                if (!g->hotkey_condition_deps.empty() &&
                    g->hotkey_condition_deps.count(var_name) == 0) return;
                auto condVal = vm_->externalRootValue(g->hotkey_condition_callback_id);
                if (!condVal) return;
                auto tracker = std::make_shared<compiler::DependencyTracker>();
                compiler::DependencyTrackerScope scope(tracker);
                bool conditionMet = false;
                try {
                    compiler::Value result = vm_->callFunctionSync(*condVal, {});
                    conditionMet = vm_->toBool(result);
                } catch (...) {}
                auto newDeps = tracker->getGlobalDependencies();
                auto fieldDeps = tracker->getFieldDependencies();
                newDeps.insert(fieldDeps.begin(), fieldDeps.end());
                g->hotkey_condition_deps.insert(newDeps.begin(), newDeps.end());
                bool prev = g->hotkey_condition_last_result;
                g->hotkey_condition_last_result = conditionMet;
                if (prev == conditionMet) return;
                pendingActions.push_back({g->hotkey_condition_alias, conditionMet});
            });
        for (auto& act : pendingActions) {
            if (act.alias.empty()) continue;
            auto* hm = vm_->hostContext() ? vm_->hostContext()->hotkeyManager : nullptr;
            if (hm) hm->SetHotkeyGrab(act.alias, act.grab);
            ::havel::stdlib::HotkeyModule::setGrab(*vm_, act.alias, act.grab);
        }
    }

    // Drain var_names queued from inside goroutine frames. Dedupes so a
    // batch of ARRAY_PUSH ops from the same window.active() call only
    // triggers one re-eval pass per unique var_name. Must be called from
    // processGoroutines (outside any fiber context).
    // Dedup is safe: reevalConditionalHotkeys reads live globals, so a
    // variable that changed multiple times during the queue accrual (mode=gaming
    // then mode=default) is re-evaluated once with the final state.
    // The Hotkey prototype's __get_<field> interceptor (OBJECT_GET) also
    // calls vm.drainConditionalHotkeyPending() when bare reads occur
    // mid-tick (e.g. `hk.grab` inside a goroutine), so grab reads don't
    // depend on reaching the tick-end drain.
    void drainPendingVarChanges() {
        std::vector<std::string> batch;
        {
            std::lock_guard<std::mutex> lk(pending_var_changes_mutex_);
            batch.swap(pending_var_changes_);
        }
        if (batch.empty()) return;
        std::unordered_set<std::string> seen;
        seen.reserve(batch.size() * 2);
        for (auto& name : batch) {
            if (seen.insert(name).second) {
                reevalConditionalHotkeys(name);
            }
        }
    }

    compiler::Value execute(const std::string& source,
                            const std::string& entryPoint = "__main__",
                            const std::string& compileUnitName = "unit",
                            bool strictSemantics = true) {
        if (!initialized_) {
            throw std::runtime_error("HavelEngine not initialized");
        }

        compiler::PipelineOptions options = modules_->options();
        options.compile_unit_name = compileUnitName;
        options.vm_override = vm_.get();
        options.strictSemantics = strictSemantics;
        for (const auto &[name, fn] : vm_->getHostFunctions()) {
            options.host_functions[name] = fn;
        }
        options.debugBytecode = config_.debugBytecode;
        options.debugEmitter = config_.debugEmitter;
        options.traceExecution = config_.traceExecution;
        options.optimizeBytecode = config_.optimizeBytecode;
        if (config_.vmConfig.max_instructions > 0 && options.max_instructions == 0) {
            options.max_instructions = config_.vmConfig.max_instructions;
        }

        // Compile to bytecode chunk (without executing)
        auto chunk = compiler::compileToBytecodeChunk(source, entryPoint, options);
        if (!chunk) {
            throw std::runtime_error("Compilation returned null chunk");
        }

        // Store chunk in VM
        auto shared_chunk = std::shared_ptr<compiler::BytecodeChunk>(std::move(chunk));
        vm_->storeMainChunk(shared_chunk);

        // Spawn the entry function as a goroutine
        auto* entryFunc = vm_->getMainChunk()->getFunction(entryPoint);
        if (!entryFunc) {
            throw std::runtime_error("Entry function not found: " + entryPoint);
        }
        uint32_t funcIndex = vm_->getMainChunk()->getFunctionIndex(entryFunc);
        compiler::Value entryCallable = compiler::Value::makeFunctionObjId(funcIndex);
        vm_->spawnGoroutine(entryCallable, {});

        // Set the script directory for relative imports
        if (!compileUnitName.empty() && compileUnitName != "unit" && compileUnitName != "script") {
            namespace fs = std::filesystem;
            std::string name = compileUnitName;
            auto plusPos = name.find(" + ");
            if (plusPos != std::string::npos)
                name = name.substr(0, plusPos);
            fs::path p(name);
            if (p.is_absolute() && fs::exists(p)) {
                vm_->setCurrentScriptDir(fs::canonical(p).parent_path().string());
            } else if (!p.is_absolute()) {
                fs::path resolved = fs::current_path() / p;
                if (fs::exists(resolved)) {
                    vm_->setCurrentScriptDir(fs::canonical(resolved).parent_path().string());
                }
            }
        }

        // Process all goroutines until they're done
        processGoroutines();

        // Return null for now (result capture would need more infrastructure)
        return compiler::Value::makeNull();
    }

    // Compile and run the entry function synchronously (not as a goroutine).
    // Does NOT call processGoroutines() - caller is responsible for driving the scheduler.
    // Useful for running a launcher script that spawns user script goroutines,
    // then driving those goroutines via an external event loop.
    compiler::Value compileAndRunMainSync(const std::string& source,
                                          const std::string& entryPoint = "__main__",
                                          const std::string& compileUnitName = "unit",
                                          bool strictSemantics = true) {
        if (!initialized_) {
            throw std::runtime_error("HavelEngine not initialized");
        }

        compiler::PipelineOptions options = modules_->options();
        options.compile_unit_name = compileUnitName;
        options.vm_override = vm_.get();
        options.strictSemantics = strictSemantics;
        for (const auto &[name, fn] : vm_->getHostFunctions()) {
            options.host_functions[name] = fn;
        }
        options.debugBytecode = config_.debugBytecode;
        options.debugEmitter = config_.debugEmitter;
        options.optimizeBytecode = config_.optimizeBytecode;
        if (config_.vmConfig.max_instructions > 0 && options.max_instructions == 0) {
            options.max_instructions = config_.vmConfig.max_instructions;
        }

        // Compile to bytecode chunk
        auto chunk = compiler::compileToBytecodeChunk(source, entryPoint, options);
        if (!chunk) {
            throw std::runtime_error("Compilation returned null chunk");
        }

        // Store chunk in VM
        auto shared_chunk = std::shared_ptr<compiler::BytecodeChunk>(std::move(chunk));
        vm_->storeMainChunk(shared_chunk);

        // Get the entry function and call it SYNCHRONOUSLY (not as goroutine)
        auto* entryFunc = vm_->getMainChunk()->getFunction(entryPoint);
        if (!entryFunc) {
            throw std::runtime_error("Entry function not found: " + entryPoint);
        }
        uint32_t funcIndex = vm_->getMainChunk()->getFunctionIndex(entryFunc);
        compiler::Value entryCallable = compiler::Value::makeFunctionObjId(funcIndex);

        // Set the script directory for relative imports
        if (!compileUnitName.empty() && compileUnitName != "unit" && compileUnitName != "script") {
            namespace fs = std::filesystem;
            std::string name = compileUnitName;
            auto plusPos = name.find(" + ");
            if (plusPos != std::string::npos)
                name = name.substr(0, plusPos);
            fs::path p(name);
            if (p.is_absolute() && fs::exists(p)) {
                vm_->setCurrentScriptDir(fs::canonical(p).parent_path().string());
            } else if (!p.is_absolute()) {
                fs::path resolved = fs::current_path() / p;
                if (fs::exists(resolved)) {
                    vm_->setCurrentScriptDir(fs::canonical(resolved).parent_path().string());
                }
            }
        }

        // Call the entry function synchronously - it may spawn goroutines via host functions
        compiler::Value result = vm_->callFunctionSync(entryCallable, {});

        // NOTE: Does NOT call processGoroutines(). Caller must drive the scheduler
        // (e.g., via tickGoroutines() in an event loop) to run spawned goroutines.
        return result;
    }

    // Load and execute pre-compiled bytecode file (.hvc)
    // This avoids recompiling the launcher from source on every invocation.
    // Spawns the entry function as a goroutine and runs processGoroutines(),
    // matching the behavior of execute() for the native pipeline.
    void executeBytecode(const std::string& hvcPath,
                         const std::string& entryPoint = "__main__",
                         const std::string& compileUnitName = "unit",
                         const std::vector<std::string>& appArgs = {},
                         bool processGoroutinesAfter = true) {
        if (!initialized_) {
            throw std::runtime_error("HavelEngine not initialized");
        }

        // Read and deserialize bytecode
        std::ifstream file(hvcPath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open bytecode file: " + hvcPath);
        }
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> buffer(size);
        if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
            throw std::runtime_error("Failed to read bytecode file: " + hvcPath);
        }

        compiler::ValueSerializer serializer;
        auto chunk_opt = serializer.deserializeChunk(buffer);
        if (!chunk_opt) {
            throw std::runtime_error("Failed to deserialize bytecode: " + hvcPath);
        }

        // Store chunk in VM
        auto shared_chunk = std::make_shared<compiler::BytecodeChunk>(std::move(*chunk_opt));
        vm_->storeMainChunk(shared_chunk);

        // Get the entry function and spawn as goroutine
        auto* entryFunc = vm_->getMainChunk()->getFunction(entryPoint);
        if (!entryFunc) {
            throw std::runtime_error("Entry function not found: " + entryPoint);
        }
        uint32_t funcIndex = vm_->getMainChunk()->getFunctionIndex(entryFunc);
        compiler::Value entryCallable = compiler::Value::makeFunctionObjId(funcIndex);

        // Set the script directory for relative imports
        if (!compileUnitName.empty() && compileUnitName != "unit" && compileUnitName != "script") {
            namespace fs = std::filesystem;
            std::string name = compileUnitName;
            auto plusPos = name.find(" + ");
            if (plusPos != std::string::npos)
                name = name.substr(0, plusPos);
            fs::path p(name);
            if (p.is_absolute() && fs::exists(p)) {
                vm_->setCurrentScriptDir(fs::canonical(p).parent_path().string());
            } else if (!p.is_absolute()) {
                fs::path resolved = fs::current_path() / p;
                if (fs::exists(resolved)) {
                    vm_->setCurrentScriptDir(fs::canonical(resolved).parent_path().string());
                }
            }
        }

        // Set app args if provided
        if (!appArgs.empty()) {
            auto arrRef = vm_->createHostArray();
            for (const auto& arg : appArgs) {
                auto strRef = vm_->createRuntimeString(arg);
                vm_->pushHostArrayValue(arrRef,
                    compiler::Value::makeStringId(strRef.id));
            }
            vm_->setAppArgs(arrRef.id);
        }

        // Spawn the entry function as a goroutine (matching execute() behavior)
        vm_->spawnGoroutine(entryCallable, {});

        // If requested, process all goroutines until they're done
        if (processGoroutinesAfter) {
            processGoroutines();
        }
    }

    void tickGoroutines() {
        if (!initialized_) return;
        auto* sched = vm_->getScheduler();
        if (!sched) return;
        vm_->tickScheduler();
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
    std::shared_ptr<HotkeyManager> hotkeyManager_;
    std::shared_ptr<WindowManager> windowManager_;
    std::shared_ptr<BrightnessManager> brightnessManager_;
#ifdef HAVEL_ENABLE_LLVM
    std::unique_ptr<compiler::BytecodeOrcJIT> jitCompiler_;
#endif
    std::unique_ptr<HostContext> hostContext_;
    std::shared_ptr<Modules> modules_;
    std::unique_ptr<compiler::WatcherRegistry> watcher_registry_;
    bool initialized_ = false;
    std::unique_ptr<compiler::Fiber> main_script_fiber_;
    bool inline_yield_active_ = false;
    // Var names produced by emitVariableChanged from inside a goroutine's
    // dispatch loop. Drained by processGoroutines between scheduler ticks so
    // conditional hotkey re-evals happen outside any fiber context.
    std::vector<std::string> pending_var_changes_;
    std::mutex pending_var_changes_mutex_;

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

  void processGoroutinesInline() {
    static const bool _trace = std::getenv("HAVEL_TRACE_CYCLE");
    if (_trace) {
      auto* _s = vm_->getScheduler();
    }
    if (inline_yield_active_) return;
    auto* sched = vm_->getScheduler();
    if (!sched) return;

    // Check if current goroutine is the one being executed
    auto* current_g = sched->current();
    (void)current_g;

    // CRITICAL: in the runBytecodePipeline hotkey path the main script is
    // NOT scheduled as a goroutine; vm_->execute runs it directly. When
    // the script blocks inside a chunked sleep host function, our
    // yield_callback fires into this method, and startGoroutineCall
    // below then wipes the shared VM stack (`while(!stack.empty())
    // stack.pop()` in startGoroutineCall). Without saving the
    // half-consumed operand stack + frame arena into a fiber here
    // and restoring them afterwards, the next CALL opcode in __main__
    // reports "CALL Underflow! Stack size: 0 Expected: 2" (e.g. the
    // script's sleep(1000) call) and the throw escapes through
    // yield_callback, silently corrupting the main loop.
    // Save main's execution state into a private Fiber first, restore
    // it before returning so the sleep host fn's chunked loop resumes
    // with main's stack intact.
    if (!main_script_fiber_) {
main_script_fiber_ = std::make_unique<compiler::Fiber>(0, 0, 0, "main-yield-snapshot");
  }
    vm_->saveFiberStatePublic(main_script_fiber_.get());

    // The caller (e.g. the current goroutine's time.sleep host fn) may have
    // an in-flight suspension_requested_ that belongs to IT, not to any
    // sibling goroutine we are about to run inline. executeOneStep() consumes
    // suspension_requested_ unconditionally (VM.cpp:~987) and calls suspend()
    // on whichever fiber it is given, so a sibling's first step here would
    // steal the caller's sleep request and both lose the sleep and misfire
    // the sibling. Preserve the caller's pending suspension, run siblings with
    // a clean VM suspension state, and restore it before reinstating main.
    const bool caller_susp = vm_->isSuspensionRequested();
    const uint8_t caller_susp_reason = vm_->getSuspensionReason();
    void* caller_susp_ctx = vm_->getSuspensionContext();
    const uint8_t caller_last_reason = vm_->getLastSuspensionReason();
    void* caller_last_ctx = vm_->getLastSuspensionContext();
    vm_->clearSuspensionRequest();
    vm_->clearLastSuspension();

    inline_yield_active_ = true;

    try {
    sched->drainDeferredCallbacks();
    sched->wakeSleepingGoroutines();
    if (std::getenv("HAVEL_TRACE_SLEEP")) {
      // fprintf(stderr, "[SLEEPDBG] processGoroutinesInline enter susp_req=%d last_reason=%d inline_active=%d\n", (int)vm_->isSuspensionRequested(), (int)vm_->getLastSuspensionReason(), (int)inline_yield_active_);
    }

    const int budget = 512;
    int executed = 0;

    while (executed < budget) {
      auto* g = sched->pickNext();
      if (_trace) {
        ::havel::info("[INLINE_YIELD] pickNext returned g={} state={}", g ? g->id : 0, g ? static_cast<int>(g->state.load()) : -1);
      }
      if (!g) break;

      if (g->state == compiler::Scheduler::GoroutineState::Created) {
        auto call_result = vm_->startGoroutineCall(g->callable, g->locals);
        if (std::getenv("HAVEL_TRACE_SLEEP")) {
          // fprintf(stderr, "[SLEEPDBG] startGoroutineCall g=%d result=%d\n", g->id, (int)call_result);
        }
        if (call_result == compiler::VM::GoroutineCallResult::Failed ||
            call_result == compiler::VM::GoroutineCallResult::JITExecuted) {
          if (g->fiber) vm_->saveFiberStatePublic(g->fiber);
          g->state = compiler::Scheduler::GoroutineState::Done;
          executed++;
          continue;
        }
        g->state = compiler::Scheduler::GoroutineState::Runnable;
        if (g->fiber) vm_->saveFiberStatePublic(g->fiber);
      } else if (g->fiber) {
        vm_->loadFiberStatePublic(g->fiber);
      }

      g->state = compiler::Scheduler::GoroutineState::Running;
      if (g->fiber) g->fiber->state = compiler::FiberState::RUNNING;

      for (int i = 0; i < 64; ++i) {
        auto result = vm_->executeOneStep(g->fiber);
        if (std::getenv("HAVEL_TRACE_SLEEP")) {
          // fprintf(stderr, "[SLEEPDBG] executeOneStep g=%d result=%d\n", g->id, (int)result.type);
        }
        g->instructions_executed++;
        executed++;
        if (result.type != compiler::VMExecutionResult::YIELD) {
          if (g->fiber && !vm_->exit_requested_.load()) vm_->saveFiberStatePublic(g->fiber);
          switch (result.type) {
            case compiler::VMExecutionResult::RETURNED:
              g->state = compiler::Scheduler::GoroutineState::Done;
              if (g->fiber) g->fiber->state = compiler::FiberState::DONE;
              break;
            case compiler::VMExecutionResult::SUSPENDED: {
              auto fiber_reason = g->fiber ? g->fiber->suspended_reason : compiler::SuspensionReason::NONE;
              void* context = g->fiber ? g->fiber->suspension_context : nullptr;
              uint8_t reason = static_cast<uint8_t>(fiber_reason);
              sched->suspend(g, toSchedulerReasonPublic(reason));
              if (fiber_reason == compiler::SuspensionReason::SLEEP) {
                int64_t ms = reinterpret_cast<intptr_t>(context);
                g->wait_handle.type = compiler::Scheduler::AwaitableType::SLEEP;
                g->wait_handle.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
              }
              if (fiber_reason == compiler::SuspensionReason::COROUTINE_WAIT) {
                uint32_t co_id = static_cast<uint32_t>(reinterpret_cast<intptr_t>(context));
                g->wait_handle.type = compiler::Scheduler::AwaitableType::COROUTINE;
                g->wait_handle.target_id = co_id;
              }
              if (fiber_reason == compiler::SuspensionReason::THREAD_JOIN) {
                uint32_t tid = static_cast<uint32_t>(reinterpret_cast<intptr_t>(context));
                g->wait_handle.type = compiler::Scheduler::AwaitableType::THREAD_JOIN;
                g->wait_handle.target_id = tid;
              }
              if (fiber_reason == compiler::SuspensionReason::CHANNEL_RECV) {
                uint32_t ch_id = static_cast<uint32_t>(reinterpret_cast<intptr_t>(context));
                g->wait_handle.type = compiler::Scheduler::AwaitableType::CHANNEL_RECV;
                g->wait_handle.target_id = ch_id;
              }
              if (fiber_reason == compiler::SuspensionReason::TIMER) {
                uint32_t timer_id = static_cast<uint32_t>(reinterpret_cast<intptr_t>(context));
                g->wait_handle.type = compiler::Scheduler::AwaitableType::TIMER_WAIT;
                g->wait_handle.target_id = timer_id;
              }
              break;
            }
            case compiler::VMExecutionResult::ERROR:
              g->state = compiler::Scheduler::GoroutineState::Done;
              if (g->fiber) g->fiber->state = compiler::FiberState::DONE;
              break;
            default:
              sched->yield(g);
              break;
          }
          break;
        }
        if (g->instructions_executed >= g->max_instructions_per_tick) {
          g->instructions_executed = 0;
          if (g->fiber) vm_->saveFiberStatePublic(g->fiber);
          sched->yield(g);
          break;
        }
        if (vm_->exit_requested_.load()) break;
      }

      if (g->state == compiler::Scheduler::GoroutineState::Running) {
        if (g->fiber) vm_->saveFiberStatePublic(g->fiber);
        sched->yield(g);
      }

      if (vm_->exit_requested_.load()) break;
    }
    } catch (...) {
      // Reset inline_yield_active_ on any throw so scheduling isn't frozen.
    }

    // Restore the main-script snapshot we saved above so the shared VM
    // stack and frame arena match __main__'s half-run state — see
    // explanation at saveFiberStatePublic above.
    if (main_script_fiber_) {
      // Reinstate the caller's own pending suspension (saved above) now that
      // sibling goroutines are done, so the caller's dispatch resumes and
      // properly suspends the CURRENT goroutine instead of a sibling.
      if (caller_susp) {
        vm_->requestSuspension(caller_susp_reason, caller_susp_ctx);
      }
      vm_->setLastSuspension(caller_last_reason, caller_last_ctx);
      vm_->loadFiberStatePublic(main_script_fiber_.get());
      if (std::getenv("HAVEL_TRACE_SLEEP")) {
        // fprintf(stderr, "[SLEEPDBG] processGoroutinesInline exit last_reason=%d susp_req=%d\n", (int)vm_->getLastSuspensionReason(), (int)vm_->isSuspensionRequested());
      }
    }

    inline_yield_active_ = false;
  }

  void processGoroutines() {
    auto* sched = vm_->getScheduler();
    if (!sched) return;

    // Ensure current_chunk is set for plain functions (not closures)
    auto mainChunk = vm_->getMainChunk();
    if (mainChunk) {
      vm_->setCurrentChunkPublic(mainChunk.get());
    }

        for (;;) {
            if (vm_->exit_requested_.load()) break;

            if (hostContext_->eventQueue) {
                hostContext_->eventQueue->processAll();
            }
            sched->drainDeferredCallbacks(compiler::FiberPriority::NORMAL);

            sched->wakeSleepingGoroutines();
            auto* g = sched->pickNext();
      if (!g) {
        size_t sc = sched->suspendedCount();
        if (sc == 0) break;
        // Persistent hotkeys park in Suspended+HotkeyWait and are woken
        // asynchronously by the input listener thread (wakeHotkey). They are
        // not a deadlock: keep pumping so the wake is picked up. Without this
        // the self-hosted path exits as soon as a hotkey-only script parks,
        // so its hotkeys can never fire (native mode covers this with the UI
        // backend's runEventLoop).
        //
        // Rate-limited STALL diagnostics: pickNext-null is a *normal* transient
        // whenever two goroutines are asleep at once. Only surface it when it
        // persists across a wall-clock second and the idle sleeper's deadline
        // has actually passed (deadlineDueMs > 0), i.e. a lost wakeup, versus
        // a still-future deadline (healthy bounded wait, < 0).
        if (std::getenv("HAVEL_TRACE_SCHED_STALL")) {
          static time_t s_last_stall_log = 0;
          time_t now_sec = ::time(nullptr);
          if (now_sec != s_last_stall_log) {
            s_last_stall_log = now_sec;
            sched->dumpGoroutineStates("HavelEngine-pickNext-null stall");
          }
        }
        if (sched->hasHotkeyWaitSuspended()) {
          std::this_thread::sleep_for(std::chrono::milliseconds(2));
          continue;
        }
        // Check if any sleeping goroutine has a deadline that will wake it
        auto deadline = sched->nextSleepDeadline();
        if (!deadline) break; // No sleeping goroutines with deadlines — would hang forever
        auto now = std::chrono::steady_clock::now();
        if (*deadline <= now) continue; // Already expired, retry immediately
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - now).count();
        auto sleepMs = std::min(ms, 100L); // Cap at 100ms to stay responsive to exit_requested
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
        continue;
      }
      

      // Set as current goroutine so VM opcodes can access it via scheduler_->current()
      sched->setCurrent(g);
      // Start and run this goroutine to completion
      // pickNext() returns goroutines with Runnable or Created state (does NOT change state).
      if (g->state == compiler::Scheduler::GoroutineState::Created) {
        std::cerr << "[DEBUG] pickNext Created gid=" << g->id << " name='" << g->name << "'\n";
        auto result = vm_->startGoroutineCall(g->callable, g->locals);
        std::cerr << "[DEBUG] startGoroutineCall gid=" << g->id << " result=" << (int)result << "\n";
        if (result != compiler::VM::GoroutineCallResult::Failed) {
          g->state = compiler::Scheduler::GoroutineState::Runnable;
          { vm_->current_executing_fiber_ = g->fiber; vm_->runDispatchLoopPublic(0); vm_->current_executing_fiber_ = nullptr; }
        } else {
          g->state = compiler::Scheduler::GoroutineState::Done;
          if (g->update_callback_id != 0) {
            vm_->releaseCallback(g->update_callback_id);
            g->update_callback_id = 0;
          }
        }
      } else if (g->state == compiler::Scheduler::GoroutineState::Runnable ||
        g->state == compiler::Scheduler::GoroutineState::Running) {
        // Update goroutines restart via startGoroutineCall (fresh each tick)
        if (g->update_interval_ms > 0) {
          auto result = vm_->startGoroutineCall(g->callable, g->locals);
          if (result != compiler::VM::GoroutineCallResult::Failed) {
            g->state = compiler::Scheduler::GoroutineState::Runnable;
            { vm_->current_executing_fiber_ = g->fiber; vm_->runDispatchLoopPublic(0); vm_->current_executing_fiber_ = nullptr; }
          } else {
            g->state = compiler::Scheduler::GoroutineState::Done;
            if (g->update_callback_id != 0) {
              vm_->releaseCallback(g->update_callback_id);
              g->update_callback_id = 0;
            }
          }
        } else {
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
          if (std::getenv("HAVEL_TRACE_RESUME")) {
            uint32_t tip = UINT32_MAX; size_t nframe = 0;
            if (g->fiber && !g->fiber->call_stack.empty()) {
              tip = g->fiber->call_stack.back().ip; nframe = g->fiber->call_stack.size();
            }
            ::havel::info("[RESUMEDBG] gid={} name='{}' topFrameIp={} nframes={} fiberIp={} waitType={} updInt={}",
                          g->id, g->name, tip, nframe,
                          g->fiber ? g->fiber->ip : UINT32_MAX,
                          (int)g->wait_handle.type, g->update_interval_ms);
          }
          { vm_->current_executing_fiber_ = g->fiber; vm_->runDispatchLoopPublic(0); vm_->current_executing_fiber_ = nullptr; }
        }
      }

      // Check if the goroutine suspended (await/sleep) or finished
      uint8_t lastReason = vm_->getLastSuspensionReason();
      if (std::getenv("HAVEL_TRACE_SLEEP")) {
        long dueMs = 0;
        uint32_t fip = UINT32_MAX; unsigned fstate = 0;
        if (g->fiber) { fip = g->fiber->ip; fstate = (unsigned)g->fiber->state; }
        {
          std::lock_guard wlock(g->wait_handle_mutex_);
          auto dl = g->wait_handle.deadline;
          if (dl != std::chrono::steady_clock::time_point{}) {
            dueMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - dl).count();
          }
        }
        std::cerr << "[SLEEPDBG] after-run gid=" << g->id << " lastReason=" << (int)lastReason
                  << " gstate=" << (int)g->state.load() << " fiberIp=" << fip
                  << " fiberState=" << fstate << " waitDueMs=" << dueMs << " updInt=" << g->update_interval_ms << "\n";
      }
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
          {
            std::lock_guard wlock(g->wait_handle_mutex_);
            g->wait_handle.type = compiler::Scheduler::AwaitableType::SLEEP;
            g->wait_handle.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
          }
        }
        if (sched->current() == g) {
          sched->clearCurrent();
        }
      } else if (g->update_interval_ms > 0) {
        // Update goroutines: reset and re-suspend with SleepWait
        sched->clearCurrent();
        g->ip = 0;
        g->stack.clear();
        g->locals.clear();
        auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(g->update_interval_ms);
        {
          std::lock_guard wlock(g->wait_handle_mutex_);
          g->wait_handle.type = compiler::Scheduler::AwaitableType::SLEEP;
          g->wait_handle.deadline = deadline;
        }
        g->state = compiler::Scheduler::GoroutineState::Suspended;
        g->suspension_reason.store(compiler::Scheduler::SuspensionReason::SleepWait, std::memory_order_release);
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
        if (g->update_callback_id != 0) {
          vm_->releaseCallback(g->update_callback_id);
          g->update_callback_id = 0;
      }
    }

    // Drain any var_names queued from inside the goroutine's dispatch
    // loop. By this point current_executing_fiber_ is null (set to nullptr
    // after runDispatchLoopPublic / yield), so conditional-hotkey re-eval
    // callbacks execute with no active fiber and cannot wedge the dispatch
    // frame that produced the VAR_CHANGED event. This runs for every
    // goroutine tick, including the last one before exit_requested_
    // terminates the loop, fixing the stale-hotkey-cache bug when a
    // goroutine calls exit(1) after mutating a condition dependency.
    drainPendingVarChanges();
  }

  // Clean up any remaining update goroutine GC roots
  {
    auto ids = sched->collectUpdateCallbackIds();
    for (auto cbid : ids) {
      vm_->releaseCallback(cbid);
    }
  }
}
};

} // namespace havel
