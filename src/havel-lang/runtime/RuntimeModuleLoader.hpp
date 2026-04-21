#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <functional>
#include <optional>

#include "../core/Value.hpp"

namespace havel {

using Value = core::Value;
using ModuleInitFn = std::function<Value(const std::vector<Value>&)>;
using NativeModuleLoader = std::function<void*(const std::string&)>;

class RuntimeModuleLoader {
public:
    struct Module {
        std::string name;
        std::string path;
        enum Type { Source, Bytecode, Native, Builtin };
        Type type;
        bool is_loaded = false;
        core::Value exports;
        void* handle = nullptr;
    };

    static RuntimeModuleLoader& getInstance();

    void addSearchPath(const std::string& path);
    void setSearchPaths(const std::vector<std::string>& paths);

    std::optional<std::string> resolve(const std::string& name);

    core::Value load(const std::string& name);
    core::Value require(const std::string& name);

    void registerBuiltin(const std::string& name, core::Value exports);
    void registerNativeLoader(NativeModuleLoader loader);

    bool isCached(const std::string& name);
    void clearCache();
    void invalidate(const std::string& name);

    std::vector<std::string> getLoadedModules() const;

private:
    RuntimeModuleLoader() = default;
    ~RuntimeModuleLoader() = default;

    std::optional<Module> findModule(const std::string& name);
    core::Value loadModule(const Module& mod);

    std::vector<std::string> search_paths_;
    std::unordered_map<std::string, Module> cache_;
    std::unordered_map<std::string, core::Value> builtin_modules_;
    NativeModuleLoader native_loader_;
    bool native_loader_registered_ = false;
    std::unordered_set<std::string> loading_; // Circular dependency detection
};

core::Value import_module(const std::vector<core::Value>& args);
core::Value load_stdlib();

}
