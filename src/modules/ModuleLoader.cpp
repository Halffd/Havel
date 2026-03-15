// ModuleLoader.cpp
// Module loading and alias registration
// Simple, explicit, no magic

#include "ModuleLoader.hpp"
#include "HostModules.hpp"
#include "havel-lang/runtime/StdLibModules.hpp"
#include "havel-lang/runtime/Environment.hpp"
#include "havel-lang/runtime/HostAPI.hpp"
#include <sstream>
#include <memory>

namespace havel::modules {

// Helper to split dotted paths
static std::vector<std::string> splitPath(const std::string& path) {
    std::vector<std::string> parts;
    std::stringstream ss(path);
    std::string item;
    while (std::getline(ss, item, '.')) {
        if (!item.empty()) {
            parts.push_back(item);
        }
    }
    return parts;
}

// Define alias like: send -> io.send
bool defineHostAlias(Environment& env, const std::string& alias,
                     const std::string& moduleName,
                     const std::string& memberPath) {
    auto moduleVal = env.Get(moduleName);
    if (!moduleVal || !moduleVal->isObject()) {
        return false;
    }

    HavelValue current = *moduleVal;
    for (const auto& part : splitPath(memberPath)) {
        if (!current.isObject()) {
            return false;
        }
        auto obj = current.asObject();
        if (!obj) {
            return false;
        }
        auto it = obj->find(part);
        if (it == obj->end()) {
            return false;
        }
        current = it->second;
    }

    env.Define(alias, current);
    return true;
}

// Define global alias
bool defineGlobalAlias(Environment& env, const std::string& alias,
                       const std::string& sourceName) {
    auto val = env.Get(sourceName);
    if (!val) {
        return false;
    }
    env.Define(alias, *val);
    return true;
}

/**
 * Load all modules (stdlib + host)
 * This is the main entry point for module loading
 */
void loadAllModules(Environment& env, std::shared_ptr<IHostAPI> hostAPI) {
    // Create module loader
    ModuleLoader loader;
    
    // Register and load stdlib modules
    registerStdLibModules(loader);
    loadStdLibModules(env, loader);
    
    // Register and load host modules
    registerHostModules(loader);
    loadHostModules(env, loader, hostAPI);
    
    // =========================================================================
    // Backwards-compatible global aliases for common host APIs
    // =========================================================================
    
    defineHostAlias(env, "send", "io", "send");
    defineHostAlias(env, "mouse", "io", "mouse");
    defineHostAlias(env, "scroll", "io", "mouse.scroll");
    defineHostAlias(env, "mousemove", "io", "mouse.move");
    defineHostAlias(env, "run", "launcher", "run");
    defineHostAlias(env, "runAsync", "launcher", "runAsync");
    defineHostAlias(env, "runDetached", "launcher", "runDetached");
    defineHostAlias(env, "runShell", "launcher", "runShell");
    defineHostAlias(env, "terminal", "launcher", "terminal");
    defineHostAlias(env, "play", "media", "play");
    defineHostAlias(env, "window.active", "window", "getActiveWindow");
    defineGlobalAlias(env, "sleep", "sleep");
}

} // namespace havel::modules
