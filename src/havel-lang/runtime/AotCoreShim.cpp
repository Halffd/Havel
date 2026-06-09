#include "havel-lang/compiler/core/BytecodeIR.hpp"
#include "havel-lang/runtime/Modules.hpp"
#include "havel-lang/compiler/vm/VM.hpp"
#include "havel-lang/runtime/HostContext.hpp"

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
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
