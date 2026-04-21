#include "RuntimeModuleLoader.hpp"
#include "../compiler/vm/VM.hpp"
#include "../compiler/core/ByteCompiler.hpp"
#include "../compiler/core/BytecodeIR.hpp"
#include "../parser/Parser.h"
#include "../core/Value.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <dlfcn.h>
#include <unordered_set>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

using havel::compiler::ByteCompiler;
using havel::parser::Parser;

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

    for (const auto& sp : search_paths_) {
        std::filesystem::path dir(sp);
        for (const auto& prefix : prefixes) {
            for (const auto& ext : extensions) {
                std::string filename = prefix + name + ext;
                std::filesystem::path file = dir / filename;
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
    
    if (loading_.count(name)) {
        throw std::runtime_error("circular dependency detected: " + name);
    }
    
    auto mod_opt = this->findModule(name);
    if (!mod_opt) {
        throw std::runtime_error("module not found: " + name);
    }
    
    loading_.insert(name);
    Value result = loadModule(*mod_opt);
    loading_.erase(name);
    
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
            ByteCompiler byteCompiler;
            Parser parser;
            
            auto program = parser.produceAST(source);
            if (!program) {
                std::cerr << "Failed to parse: " << mod.path << "\n";
                return Value::makeNull();
            }
            
            auto chunk = byteCompiler.compile(*program);
            vm.execute(*chunk, "__main__");
            
            const auto& allGlobals = vm.getAllGlobals();
            
            cache_[mod.name] = mod;
            cache_[mod.name].is_loaded = true;
            cache_[mod.name].exports = core::Value::makeNull();
            return cache_[mod.name].exports;
        }
        
        case Module::Bytecode: {
            int fd = open(mod.path.c_str(), O_RDONLY);
            if (fd < 0) {
                std::cerr << "Failed to open bytecode: " << mod.path << "\n";
                return Value::makeNull();
            }
            
            struct stat st;
            if (fstat(fd, &st) < 0) {
                close(fd);
                std::cerr << "Failed to stat bytecode: " << mod.path << "\n";
                return Value::makeNull();
            }
            
            size_t size = st.st_size;
            if (size < 8) {
                close(fd);
                std::cerr << "Bytecode file too small: " << mod.path << "\n";
                return Value::makeNull();
            }
            
            void* data = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
            close(fd);
            
            if (data == MAP_FAILED) {
                std::cerr << "Failed to mmap bytecode: " << mod.path << "\n";
                return Value::makeNull();
            }
            
            const char* magic = static_cast<const char*>(data);
            if (magic[0] != 'H' || magic[1] != 'B' || magic[2] != 'C' || magic[3] != 0) {
                munmap(data, size);
                std::cerr << "Invalid bytecode magic: " << mod.path << "\n";
                return Value::makeNull();
            }
            
            uint32_t version = *reinterpret_cast<const uint32_t*>(magic + 4);
            if (version != 1) {
                munmap(data, size);
                std::cerr << "Unsupported bytecode version: " << version << "\n";
                return Value::makeNull();
            }
            
            munmap(data, size);
            
            cache_[mod.name] = mod;
            cache_[mod.name].is_loaded = true;
            cache_[mod.name].exports = core::Value::makeNull();
            return cache_[mod.name].exports;
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
            
            auto init_fn_ptr = dlsym(handle, "havel_module_init");
            if (!init_fn_ptr) {
                std::cerr << "No havel_module_init in: " << mod.path << "\n";
                dlclose(handle);
                return Value::makeNull();
            }
            using InitFuncType = Value(*)(const std::vector<Value>&);
            InitFuncType init = reinterpret_cast<InitFuncType>(init_fn_ptr);
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