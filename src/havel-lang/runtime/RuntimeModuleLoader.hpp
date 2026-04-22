#pragma once

#include <string>
#include <vector>
#include <optional>
#include <functional>

#include "../core/Value.hpp"

namespace havel {

// Forward declare VM to avoid circular include
namespace compiler { class VM; }

// RuntimeModuleLoader - Thin shim that delegates to VM::loadModule()
// DEPRECATED: Use VM::loadModule() or VM::moduleLoader() directly instead.
// This class exists only for backward compatibility with the C API.
class RuntimeModuleLoader {
public:
    // No longer a singleton - constructed with a VM reference
    explicit RuntimeModuleLoader(compiler::VM& vm) : vm_(vm) {}

    // Legacy singleton access (DEPRECATED - returns a default-VM instance)
    static RuntimeModuleLoader& getInstance();

    void addSearchPath(const std::string& path);
    void setSearchPaths(const std::vector<std::string>& paths);

    std::optional<std::string> resolve(const std::string& name);
    core::Value load(const std::string& name);
    core::Value require(const std::string& name);

    void registerBuiltin(const std::string& name, core::Value exports);
    void registerNativeLoader(std::function<void*(const std::string&)> loader);

    bool isCached(const std::string& name);
    void clearCache();
    void invalidate(const std::string& name);

    std::vector<std::string> getLoadedModules() const;

private:
    compiler::VM& vm_;
};

} // namespace havel