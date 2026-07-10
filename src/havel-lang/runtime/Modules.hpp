#pragma once

#include "HostContext.hpp"
#include "dl/Loader.hpp"
#include "../compiler/core/Pipeline.hpp"
#include "../compiler/vm/VMApi.hpp"
#include "../compiler/vm/VM.hpp"
#include "../compiler/runtime/EventQueue.hpp"
#include "../../host/module/ExecutionPolicy.hpp"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace havel::compiler {
class ConcurrencyBridge;
class IOBridge;
class SystemBridge;
class UIBridge;
class InputBridge;
class MediaBridge;
class AudioBridge;
class DisplayBridge;
class ConfigBridge;
class ModeBridge;
class TimerBridge;
class AppBridge;
class AutomationBridge;
class BrowserBridge;
class ToolsBridge;
}

using havel::compiler::CallbackId;

namespace havel {

enum class InstallProfile { Full, Core };

class Modules {
public:
    explicit Modules(const HostContext &ctx);
    Modules(const HostContext &ctx, const compiler::ExecutionPolicy &policy);
    ~Modules();

    Modules(const Modules &) = delete;
    Modules &operator=(const Modules &) = delete;

    void install(InstallProfile profile = InstallProfile::Full,
                 bool eagerBridges = true);
    void shutdown();

    compiler::PipelineOptions &options() { return options_; }
    const compiler::PipelineOptions &options() const { return options_; }
    const HostContext &context() const { return *ctx_; }

    Loader &extensionLoader() { return *extensionLoader_; }
    const Loader &extensionLoader() const { return *extensionLoader_; }

    compiler::EventQueue *eventQueue() const;
    void checkTimers();

    bool loadModule(const std::string &name);
    bool import(const std::string &importSpec);

private:
    const HostContext *ctx_;
    compiler::PipelineOptions options_;
    compiler::ExecutionPolicy policy_;
    InstallProfile profile_ = InstallProfile::Full;

    std::unique_ptr<Loader> extensionLoader_;

    std::unique_ptr<compiler::ConcurrencyBridge> concurrencyBridge_;
    std::unique_ptr<compiler::IOBridge> ioBridge_;
    std::unique_ptr<compiler::SystemBridge> systemBridge_;
    std::unique_ptr<compiler::UIBridge> uiBridge_;
    std::unique_ptr<compiler::InputBridge> inputBridge_;
    std::unique_ptr<compiler::MediaBridge> mediaBridge_;
    std::unique_ptr<compiler::AudioBridge> audioBridge_;
    std::unique_ptr<compiler::DisplayBridge> displayBridge_;
    std::unique_ptr<compiler::ConfigBridge> configBridge_;
    std::unique_ptr<compiler::ModeBridge> modeBridge_;
    std::unique_ptr<compiler::TimerBridge> timerBridge_;
    std::unique_ptr<compiler::AppBridge> appBridge_;
    std::unique_ptr<compiler::AutomationBridge> automationBridge_;
    std::unique_ptr<compiler::BrowserBridge> browserBridge_;
    std::unique_ptr<compiler::ToolsBridge> toolsBridge_;

    std::vector<std::function<void(compiler::VM &)>> vm_setup_callbacks_;

    void initBridges();
    void installHostFunctions();
    void installStdLib();
};

void registerPureStdLib(compiler::VM &vm);
void registerCoreStdLib(compiler::VM &vm);
std::unique_ptr<Modules> createModules(const HostContext &ctx);

} // namespace havel
