#include "RuntimeModuleLoader.hpp"
#include "../compiler/vm/VM.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <dlfcn.h>

namespace havel {

RuntimeModuleLoader& RuntimeModuleLoader::getInstance() {
    static RuntimeModuleLoader instance;
    return instance;
}

void RuntimeModuleLoader::addSearchPath(const std::string& path) {
    search_paths_.push_back(path);
}

void RuntimeModuleLoader::setSearchPaths(const std::vector<std::string>& paths) {
    search_paths_ = paths;
}

std::optional<std::string> RuntimeModuleLoader::resolve(const std::string& name) {
    auto mod = this->findModule(name);
    if (mod) {
        return mod->path;
    }
    return std::nullopt;
}

std::optional<RuntimeModuleLoader::Module> RuntimeModuleLoader::findModule(const std::string& name) {
    std::vector<std::string> extensions = {".hv", ".hbc", ".so", ""};
    std::vector<std::string> prefixes = {"", "libhavel_"};

    for (const auto& path : search_paths_) {
        for (const auto& prefix : prefixes) {
            for (const auto& ext : extensions) {
                std::filesystem::path file = path / (prefix + name + ext);
                if (std::filesystem::exists(file)) {
                    Module m;
                    m.name = name;
                    m.path = file.string();
                    if (ext == ".hv") m.type = Module::Source;
                    else if (ext == ".hbc") m.type = Module::Bytecode;
                    else if (ext == ".so") m.type = Module::Native;
                    else m.type = Module::Source;
                    return m;
                }
            }
        }
    }
    return std::nullopt;
}

Value RuntimeModuleLoader::load(const std::string& name) {
    auto it = cache_.find(name);
    if (it != cache_.end() && it->second.is_loaded) {
        return it->second.exports;
    }
    
    auto mod_opt = this->findModule(name);
    if (!mod_opt) {
        std::cerr << "Module not found: " << name << "\n";
        return Value::makeNull();
    }
    
    return loadModule(*mod_opt);
}

Value RuntimeModuleLoader::require(const std::string& name) {
    auto it = cache_.find(name);
    if (it != cache_.end() && it->second.is_loaded) {
        return it->second.exports;
    }
    
    auto mod_opt = this->findModule(name);
    if (!mod_opt) {
        throw std::runtime_error("module not found: " + name);
    }
    
    Value result = loadModule(*mod_opt);
    cache_[name] = *mod_opt;
    cache_[name].is_loaded = true;
    cache_[name].exports = result;
    
    return result;
}

Value RuntimeModuleLoader::loadModule(const Module& mod) {
    switch (mod.type) {
        case Module::Source: {
            std::ifstream file(mod.path);
            std::string source((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
            
            compiler::VM vm;
            vm.load(source, mod.name);
            vm.run();
            
            cache_[mod.name] = mod;
            cache_[mod.name].is_loaded = true;
            cache_[mod.name].exports = Value::makeNull();
            return cache_[mod.name].exports;
        }
        
        case Module::Bytecode: {
            std::cerr << "Bytecode loading not implemented: " << mod.path << "\n";
            return Value::makeNull();
        }
        
        case Module::Native: {
            if (!native_loader_registered_ && native_loader_) {
                void* handle = native_loader_(mod.path);
                if (handle) {
                    cache_[mod.name] = mod;
                    cache_[mod.name].is_loaded = true;
                    cache_[mod.name].handle = handle;
                    return Value::makePtr(handle);
                }
            }
            
            void* handle = dlopen(mod.path.c_str(), RTLD_LAZY);
            if (!handle) {
                std::cerr << "Failed to load native: " << dlerror() << "\n";
                return Value::makeNull();
            }
            
            auto init = (ModuleInitFn)dlsym(handle, "havel_module_init");
            if (!init) {
                std::cerr << "No havel_module_init in: " << mod.path << "\n";
                dlclose(handle);
                return Value::makeNull();
            }
            
            Value exports = init({});
            cache_[mod.name] = mod;
            cache_[mod.name].is_loaded = true;
            cache_[mod.name].handle = handle;
            cache_[mod.name].exports = exports;
            return exports;
        }
        
        case Module::Builtin: {
            auto it = builtin_modules_.find(mod.name);
            if (it != builtin_modules_.end()) {
                cache_[mod.name] = mod;
                cache_[mod.name].is_loaded = true;
                cache_[mod.name].exports = it->second;
                return it->second;
            }
            return Value::makeNull();
        }
    }
    
    return Value::makeNull();
}

void RuntimeModuleLoader::registerBuiltin(const std::string& name, Value exports) {
    builtin_modules_[name] = exports;
    
    Module m;
    m.name = name;
    m.type = Module::Builtin;
    cache_[name] = m;
    cache_[name].is_loaded = true;
    cache_[name].exports = exports;
}

void RuntimeModuleLoader::registerNativeLoader(NativeModuleLoader loader) {
    native_loader_ = loader;
    native_loader_registered_ = true;
}

bool RuntimeModuleLoader::isCached(const std::string& name) {
    auto it = cache_.find(name);
    return it != cache_.end() && it->second.is_loaded;
}

void RuntimeModuleLoader::clearCache() {
    for (auto& [name, mod] : cache_) {
        if (mod.type == Module::Native && mod.handle) {
            dlclose(mod.handle);
        }
    }
    cache_.clear();
}

void RuntimeModuleLoader::invalidate(const std::string& name) {
    auto it = cache_.find(name);
    if (it != cache_.end()) {
        if (it->second.type == Module::Native && it->second.handle) {
            dlclose(it->second.handle);
        }
        cache_.erase(it);
    }
}

std::vector<std::string> RuntimeModuleLoader::getLoadedModules() const {
    std::vector<std::string> result;
    for (const auto& [name, mod] : cache_) {
        if (mod.is_loaded) {
            result.push_back(name);
        }
    }
    return result;
}

Value import_module(const std::vector<Value>& args) {
    if (args.empty()) {
        return Value::makeNull();
    }
    return RuntimeModuleLoader::getInstance().require("dummy");
}

Value load_stdlib() {
    auto& loader = RuntimeModuleLoader::getInstance();
    
    loader.addSearchPath(".");
    loader.addSearchPath("./lib");
    loader.addSearchPath("/usr/local/lib/havel");
    loader.addSearchPath("/usr/lib/havel");
    
    if (auto home = std::getenv("HOME")) {
        std::string user_path = std::string(home) + "/.havel/modules";
        loader.addSearchPath(user_path);
    }
    
    if (auto path = std::getenv("HAVEL_PATH")) {
        loader.addSearchPath(path);
    }
    
    return Value::makeNull();
}

}