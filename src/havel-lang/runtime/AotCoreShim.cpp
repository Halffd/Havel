#include "havel-lang/compiler/core/BytecodeIR.hpp"
#include "havel-lang/runtime/Modules.hpp"
#include "havel-lang/compiler/vm/VM.hpp"
#include "havel-lang/runtime/HostContext.hpp"
#include "core/util/Env.hpp"

#include <cstdint>
#include <memory>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif
extern "C" void* havel_vm_init_standalone_core(const char** strings, uint32_t count) {
    using namespace havel;

    static HostContext ctx;
    static std::unique_ptr<compiler::VM> vm;
    static std::shared_ptr<Modules> modules;
    static std::shared_ptr<compiler::BytecodeChunk> keep_alive;

    if (!vm) {
        vm = std::make_unique<compiler::VM>(ctx);
        ctx.vm = vm.get();

        modules = havel::createModules(ctx);
        modules->install(havel::InstallProfile::Core, false);
        for (const auto &[name, fn] : modules->options().host_functions) {
            vm->registerHostFunction(name, fn);
        }
        
        // Initialize module loader for AOT so that IMPORT works
        if (vm) {
            // Set stdlib path - always derive from executable path (source tree)
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
            vm->moduleLoader().setStdlibPath(stdlibPath);

            // Add module search paths - also derive from executable path
            std::string modulesRoot;
            auto exePath = Env::executable();
            if (!exePath.empty()) {
                modulesRoot = (std::filesystem::path(exePath).parent_path() / ".." / "modules").string();
            } else {
                modulesRoot = "./modules";
            }
            auto canonicalRoot = std::filesystem::exists(modulesRoot)
                ? std::filesystem::canonical(modulesRoot).string() : modulesRoot;
            vm->moduleLoader().addSearchPath(canonicalRoot + "/lang");
            vm->moduleLoader().addSearchPath(canonicalRoot + "/std");
            vm->moduleLoader().addSearchPath(canonicalRoot + "/app");
            vm->moduleLoader().addSearchPath(canonicalRoot);
        }
    }

    if (strings && count > 0) {
        auto chunk = std::make_shared<compiler::BytecodeChunk>();
        for (uint32_t i = 0; i < count; ++i) {
            chunk->addString(strings[i]);
        }
        vm->setCurrentChunkPublic(chunk.get());
        vm->pushFramePublic(nullptr, 0, 0, 0);
        keep_alive = std::move(chunk);
    }

    return vm.get();
}

// Extended init for AOT with closures (core profile): also creates function entries
extern "C" void* havel_vm_init_standalone_with_functions_core(
    const char** strings, uint32_t string_count,
    const char** func_names, uint32_t func_count,
    const uint32_t* func_param_counts, const uint32_t* func_local_counts,
    const uint32_t* func_upvalue_counts, const uint32_t* func_is_generator,
    const uint32_t* upvalue_indices, const uint32_t* upvalue_captures_local,
    uint32_t total_upvalues,
    const char* build_dir
) {
    using namespace havel;

    static HostContext ctx;
    static std::unique_ptr<compiler::VM> vm;
    static std::shared_ptr<Modules> modules;
    static std::shared_ptr<compiler::BytecodeChunk> keep_alive;

    if (!vm) {
        vm = std::make_unique<compiler::VM>(ctx);
        ctx.vm = vm.get();

        modules = havel::createModules(ctx);
        
        modules->install(havel::InstallProfile::Full, false);
        
        // Add build directory to C loader's search paths for native modules AFTER install
        if (build_dir && build_dir[0] != '\0') {
            auto& extLoader = modules->extensionLoader();
            std::string buildModulesPath = (std::filesystem::path(build_dir) / "modules").string();
            extLoader.addSearchPath(buildModulesPath);
        }
        
        for (const auto &[name, fn] : modules->options().host_functions) {
            vm->registerHostFunction(name, fn);
        }
        
        // Initialize module loader for AOT so that IMPORT works
        if (vm) {
            // Set stdlib path - always derive from executable path (source tree)
            std::string stdlibPath;
            const char* envStdlib = std::getenv("HAVEL_STDLIB");
            if (envStdlib && envStdlib[0] != '\0') {
                stdlibPath = envStdlib;
            } else if (build_dir && build_dir[0] != '\0') {
                stdlibPath = (std::filesystem::path(build_dir) / ".." / "modules" / "std").string();
            } else {
                auto exePath = Env::executable();
                if (!exePath.empty()) {
                    stdlibPath = (std::filesystem::path(exePath).parent_path() / ".." / "modules" / "std").string();
                } else {
                    stdlibPath = "./modules/std";
                }
            }
            vm->moduleLoader().setStdlibPath(stdlibPath);

            // Add module search paths - also derive from executable path
            std::string modulesRoot;
            auto exePath = Env::executable();
            if (!exePath.empty()) {
                modulesRoot = (std::filesystem::path(exePath).parent_path() / ".." / "modules").string();
            } else {
                modulesRoot = "./modules";
            }
            auto canonicalRoot = std::filesystem::exists(modulesRoot)
                ? std::filesystem::canonical(modulesRoot).string() : modulesRoot;
            vm->moduleLoader().addSearchPath(canonicalRoot + "/lang");
            vm->moduleLoader().addSearchPath(canonicalRoot + "/std");
            vm->moduleLoader().addSearchPath(canonicalRoot + "/app");
            vm->moduleLoader().addSearchPath(canonicalRoot);
            
            // Also add build directory to C loader's search paths for native modules
            if (build_dir && build_dir[0] != '\0') {
                auto extLoader = vm->pluginLoader();
                if (extLoader) {
                    extLoader->addSearchPath((std::filesystem::path(build_dir) / "modules").string());
                }
            }
        }
    }

    if (strings && string_count > 0) {
        auto chunk = std::make_shared<compiler::BytecodeChunk>();
        for (uint32_t i = 0; i < string_count; ++i) {
            chunk->addString(strings[i]);
        }
        
        // Parse upvalue data
        uint32_t upvalue_offset = 0;
        for (uint32_t fi = 0; fi < func_count; ++fi) {
            std::string name(func_names[fi]);
            uint32_t param_count = func_param_counts[fi];
            uint32_t local_count = func_local_counts[fi];
            uint32_t upvalue_count = func_upvalue_counts[fi];
            bool is_gen = func_is_generator[fi] != 0;
            
            compiler::BytecodeFunction func(name, param_count, local_count);
            func.is_generator = is_gen;
            
            // Add upvalue descriptors
            for (uint32_t ui = 0; ui < upvalue_count; ++ui) {
                compiler::UpvalueDescriptor desc;
                desc.index = upvalue_indices[upvalue_offset + ui];
                desc.captures_local = upvalue_captures_local[upvalue_offset + ui] != 0;
                func.upvalues.push_back(desc);
            }
            upvalue_offset += upvalue_count;
            
            chunk->addFunction(std::move(func));
        }
        
        vm->setCurrentChunkPublic(chunk.get());
        vm->pushFramePublic(nullptr, 0, 0, 0);
        keep_alive = std::move(chunk);
    }

    return vm.get();
}
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif