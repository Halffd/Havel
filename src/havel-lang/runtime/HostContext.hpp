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

namespace compiler { class VM; }


// Forward declaration

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
namespace host {
class AsyncService;
}
class WindowMonitor;  // Forward declaration for window monitoring

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
 * ARCHITECTURE:
 * - HostContext does NOT own services (raw pointers)
 * - VM is infrastructure, not a service (non-owning pointer)
 * - Services are pure C++ (no VM types)
 * - Only HostBridge and high-level systems touch VM
 * 
 * Usage:
 *   auto vm = std::make_unique<VM>(ctx);
 *   HostContext ctx;
 *   ctx.vm = vm.get();  // Non-owning
 *   ctx.io = io.get();  // Non-owning
 *   
 *   auto bridge = std::make_unique<HostBridge>(ctx);
 */
struct HostContext {
    // VM infrastructure (non-owning - only high-level systems use this)
    class compiler::VM* vm = nullptr;
    
    // Reference accessor - VM is always present when HostContext is used
    compiler::VM& getVM() { return *vm; }
    const compiler::VM& getVM() const { return *vm; }
    
    // Core services (non-owning pointers)
    class IO* io = nullptr;
    class WindowManager* windowManager = nullptr;
    class HotkeyManager* hotkeyManager = nullptr;
    class ModeManager* modeManager = nullptr;
    class BrightnessManager* brightnessManager = nullptr;
    class AudioManager* audioManager = nullptr;
    class GUIManager* guiManager = nullptr;
    class ScreenshotManager* screenshotManager = nullptr;
    class ClipboardManager* clipboardManager = nullptr;
    class PixelAutomation* pixelAutomation = nullptr;
    class AutomationManager* automationManager = nullptr;
    class FileManager* fileManager = nullptr;
    class ProcessManager* processManager = nullptr;
    class net::NetworkManager* networkManager = nullptr;
    class host::AsyncService* asyncService = nullptr;
    class WindowMonitor* windowMonitor = nullptr;  // Window monitoring for dynamic variables
    
    // Capability-based extensions (embedder-provided)
    std::unordered_map<std::string, std::shared_ptr<Capability>> caps;
    
    // Helper to get capability with type safety
    template<typename T>
    T* getCap(const std::string& name) const {
        auto it = caps.find(name);
        if (it == caps.end()) return nullptr;
        return dynamic_cast<T*>(it->second.get());
    }
    
    // Validation (io required for full mode, optional for pure script execution)
    bool isValid() const { return true; }
    bool hasIO() const { return io != nullptr; }
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
