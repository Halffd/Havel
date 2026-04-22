#pragma once

#include "../compiler/vm/VM.hpp"
#include "../compiler/runtime/HostBridge.hpp"
#include "../compiler/core/Pipeline.hpp"
#include "StdLibModules.hpp"
#include "HostAPI.hpp"
#include "../../modules/HostModules.hpp"
#include "../../host/ServiceRegistry.hpp"
#include <memory>
#include <string>

namespace havel {

struct EngineConfig {
    bool debugBytecode = false;
    bool debugLexer = false;
    bool debugParser = false;
    bool debugAst = false;
    bool stopOnError = false;
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
        initializeFull(hostAPI);
    }

    void initializeFull(std::shared_ptr<IHostAPI> hostAPI) {
        if (initialized_) return;

        host::ServiceRegistry::instance().clear();
        if (hostAPI && hostAPI->GetIO()) {
            initializeServiceRegistry(hostAPI);
        }

        hostContext_ = std::make_unique<HostContext>(createHostContext(hostAPI));

        vm_ = std::make_shared<compiler::VM>(*hostContext_);
        hostContext_->vm = vm_.get();

        hostBridge_ = compiler::createHostBridge(*hostContext_);
        registerStdLibWithVM(*hostBridge_);
        hostBridge_->install();

        for (const auto& [name, fn] : hostBridge_->options().host_functions) {
            vm_->registerHostFunction(name, fn);
        }

        vm_->setTimerCheckFunction([this]() { hostBridge_->checkTimers(); });

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
    bool initialized_ = false;
};

} // namespace havel
