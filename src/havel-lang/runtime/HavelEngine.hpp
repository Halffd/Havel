#pragma once

#include "../compiler/vm/VM.hpp"
#include "../compiler/runtime/HostBridge.hpp"
#include "../compiler/core/Pipeline.hpp"
#include "StdLibModules.hpp"
#include "HostAPI.hpp"
#include "../../modules/HostModules.hpp"
#include "../../host/ServiceRegistry.hpp"
#include "../../core/util/Env.hpp"
#include "../runtime/concurrency/WatcherRegistry.hpp"
#include "../runtime/concurrency/Scheduler.hpp"
#include "../runtime/concurrency/Fiber.hpp"
#include "core/config/ConfigManager.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <cstdlib>

namespace havel {

struct EngineConfig {
    bool debugBytecode = false;
    bool debugLexer = false;
    bool debugParser = false;
    bool debugAst = false;
    bool stopOnError = false;
    bool leanMinimalStartup = false;
};

class HavelEngine {
public:
    explicit HavelEngine(const EngineConfig& config = {})
        : config_(config) {}

    ~HavelEngine() { shutdown(); }

    HavelEngine(const HavelEngine&) = delete;
    HavelEngine& operator=(const HavelEngine&) = delete;

    void initializeMinimal() {
        auto hostAPI = std::make_shared<HostAPI>(nullptr, nullptr, Configs::Get());
        initializeFull(hostAPI, config_.leanMinimalStartup);
    }

  void initializeFull(std::shared_ptr<IHostAPI> hostAPI, bool leanStartup = false) {
    if (initialized_) return;

    host::ServiceRegistry::instance().clear();
    initializeServiceRegistry(hostAPI);

        hostContext_ = std::make_unique<HostContext>(createHostContext(hostAPI));

        vm_ = std::make_shared<compiler::VM>(*hostContext_);
        hostContext_->vm = vm_.get();

        
        // Set up scheduler for goroutine/thread support
        vm_->setScheduler(&compiler::Scheduler::instance());

        hostBridge_ = compiler::createHostBridge(*hostContext_);

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
            registerPureStdLib(*vm_);
        } else {
            registerStdLibWithVM(*hostBridge_);
        }
        vm_->resumeGC();
        hostBridge_->install(
            leanStartup ? compiler::HostBridge::InstallProfile::Core
                        : compiler::HostBridge::InstallProfile::Full,
            !leanStartup);

        for (const auto& [name, fn] : hostBridge_->options().host_functions) {
            vm_->registerHostFunction(name, fn);
        }

        vm_->setTimerCheckFunction([this]() { hostBridge_->checkTimers(); });

        // Wire watcher registry for reactive when blocks
        watcher_registry_ = std::make_unique<compiler::WatcherRegistry>();
        vm_->setWatcherRegistry(watcher_registry_.get());

        initialized_ = true;
    }

    compiler::Value execute(const std::string& source,
                            const std::string& entryPoint = "__main__",
                            const std::string& compileUnitName = "unit") {
        if (!initialized_) {
            throw std::runtime_error("HavelEngine not initialized");
        }

        compiler::PipelineOptions options = hostBridge_->options();
        options.compile_unit_name = compileUnitName;
        options.vm_override = vm_.get();
        options.debugBytecode = config_.debugBytecode;

        auto result = compiler::runBytecodePipeline(source, entryPoint, options);

        
        // Process pending scheduler goroutines after main script completes
        processGoroutines();

        return result.return_value;
    }

    compiler::VM* vm() const { return vm_.get(); }
    compiler::HostBridge* hostBridge() const { return hostBridge_.get(); }
    bool isInitialized() const { return initialized_; }

    void shutdown() {
        if (hostBridge_) {
            hostBridge_->shutdown();
        }
        vm_.reset();
        hostBridge_.reset();
        hostContext_.reset();
        initialized_ = false;
    }

private:
	EngineConfig config_;
	std::shared_ptr<compiler::VM> vm_;
	std::unique_ptr<HostContext> hostContext_;
	std::shared_ptr<compiler::HostBridge> hostBridge_;
	std::unique_ptr<compiler::WatcherRegistry> watcher_registry_;
	bool initialized_ = false;

        void processGoroutines() {
            auto* sched = vm_->getScheduler();
            if (!sched) return;

            
            // Ensure current_chunk is set for plain functions (not closures)
            auto mainChunk = vm_->getMainChunk();
            if (mainChunk) {
                vm_->setCurrentChunkPublic(mainChunk.get());
            }

            bool anyExecuted = false;
            do {
                anyExecuted = false;
                sched->wakeSleepingGoroutines();
                auto* g = sched->pickNext();
                if (!g) {
                    if (sched->suspendedCount() > 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        continue;
                    }
                    break;
                }

                // Start and run this goroutine to completion
                if (g->state == compiler::Scheduler::GoroutineState::Created) {
                    bool ok = vm_->startGoroutineCall(g->function_id, g->closure_id, g->locals);
                    if (ok) {
                        g->state = compiler::Scheduler::GoroutineState::Runnable;
                        vm_->runDispatchLoopPublic(0);
                        anyExecuted = true;
                    }
                }
                g->state = compiler::Scheduler::GoroutineState::Done;
                if (g->fiber) {
                    g->fiber->state = compiler::FiberState::DONE;
                }
            } while (anyExecuted);
        }
};

} // namespace havel
