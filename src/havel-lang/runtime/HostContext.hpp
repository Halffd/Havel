/*
 * HostContext.hpp - Injected dependencies for HostBridge
 * 
 * Instead of pulling services from a global registry,
 * dependencies are pushed into HostContext by the embedder.
 * 
 * This enables:
 * - No hidden dependencies
 * - Easier embedding
 * - Easier testing with mocks
 * - Capability-based access control
 */
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <functional>
#include <optional>
#include <variant>

namespace havel {

// Forward declarations
class IO;
class HotkeyManager;
class WindowManager;
class ModeManager;
class BrightnessManager;
class AudioManager;
class GUIManager;
class ScreenshotManager;
class ClipboardManager;
class PixelAutomation;
class AutomationManager;
class FileManager;
class ProcessManager;

namespace net {
class NetworkManager;
}

/**
 * Capability - Base interface for pluggable host functionality
 * 
 * Embedders can provide custom implementations:
 *   struct MyFileSystem : Capability { ... };
 *   ctx.caps["fs"] = std::make_shared<MyFileSystem>();
 */
struct Capability {
    virtual ~Capability() = default;
    virtual std::string typeName() const = 0;
};

/**
 * HostContext - Injected dependencies for HostBridge
 * 
 * Usage:
 *   HostContext ctx;
 *   ctx.io = std::make_shared<IO>();
 *   ctx.caps["custom"] = std::make_shared<MyCustomCap>();
 *   
 *   auto bridge = std::make_unique<HostBridge>(ctx);
 */
struct HostContext {
    // Core services (required for basic functionality)
    std::shared_ptr<IO> io;
    
    // Optional services (nullptr if not available)
    std::shared_ptr<HotkeyManager> hotkeyManager;
    std::shared_ptr<WindowManager> windowManager;
    std::shared_ptr<ModeManager> modeManager;
    std::shared_ptr<BrightnessManager> brightnessManager;
    std::shared_ptr<AudioManager> audioManager;
    std::shared_ptr<GUIManager> guiManager;
    std::shared_ptr<ScreenshotManager> screenshotManager;
    std::shared_ptr<ClipboardManager> clipboardManager;
    std::shared_ptr<PixelAutomation> pixelAutomation;
    std::shared_ptr<AutomationManager> automationManager;
    std::shared_ptr<FileManager> fileManager;
    std::shared_ptr<ProcessManager> processManager;
    std::shared_ptr<net::NetworkManager> networkManager;
    
    // Capability-based extensions (embedder-provided)
    std::unordered_map<std::string, std::shared_ptr<Capability>> caps;
    
    // Helper to get capability with type safety
    template<typename T>
    std::shared_ptr<T> getCap(const std::string& name) const {
        auto it = caps.find(name);
        if (it == caps.end()) return nullptr;
        return std::dynamic_pointer_cast<T>(it->second);
    }
    
    // Validation
    bool isValid() const { return io != nullptr; }
};

/**
 * HostModule - Pluggable module for HostBridge
 * 
 * Embedders can register custom modules:
 *   HostModule mod;
 *   mod.name = "engine";
 *   mod.functions["spawn_window"] = [](args) { ... };
 *   bridge.registerModule(mod);
 */
using HostFn = std::function<std::variant<std::nullptr_t, bool, int64_t, double, std::string>(
    const std::vector<std::variant<std::nullptr_t, bool, int64_t, double, std::string>>&)>;

struct HostModule {
    std::string name;
    std::unordered_map<std::string, HostFn> functions;
    
    void registerFn(const std::string& name, HostFn fn) {
        functions[name] = std::move(fn);
    }
};

} // namespace havel
