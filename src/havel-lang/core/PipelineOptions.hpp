#pragma once

// PipelineOptions - how a caller wants source compiled.
//
// This lives outside compiler/core/Pipeline.hpp on purpose. It is a plain
// configuration record built only from runtime types (strings, bools, a
// host-function table, a VM pointer, a few callbacks), and the runtime
// keeps one: Modules, HostBridge and ConcurrencyBridge all carry options
// that are handed to whichever compiler is registered. Declaring it next to
// the pipeline entry points forced every runtime header holding options to
// include a header that also pulls in the parser and the type checker, so a
// runtime-only consumer could not even name its own configuration without
// dragging the frontend in.
//
// The functions that consume it - runBytecodePipeline, compileToBytecodeChunk
// - stay in compiler/core/Pipeline.hpp, because they are the compiler.
// Runtime callers reach the compiler through ModuleCompilerHook.

#include "havel-lang/compiler/core/BytecodeIR.hpp"
#include <functional>
#include <string>
#include <unordered_map>
#include <cstdint>

namespace havel::compiler {

class VM;

struct PipelineOptions {
    std::string compile_unit_name = "unit";
    std::string snapshot_dir;
    bool write_snapshot_artifact = false;
    bool debugBytecode = false;
    bool debugEmitter = false;
    bool traceExecution = false;
    // Run the CFG optimization pipeline (reconstruct -> passes -> lower) over
    // compiled functions. Functions with opcodes the CFG model cannot carry
    // (exception handlers, inline caches, coroutine suspension) are skipped.
    bool optimizeBytecode = false;
    uint64_t max_instructions = 0; // 0 = unlimited
    std::unordered_map<std::string, BytecodeHostFunction> host_functions;
    VM *vm_override = nullptr;
    std::function<void(VM &)> vm_setup;
    std::function<void(VM *)> system_object_initializer; // Create system object with proper namespacing
    // Optional yield hook invoked from the main fiber dispatch loop
    // (sleep host function chunked path). Default callback only drains
    // pending events and wakes sleeping goroutines; a richer caller
    // (HavelEngine) supplies a callback that also pumps the scheduler
    // so spawned goroutines get a chance to run while main blocks in
    // a long sleep.
    std::function<void()> yield_callback;
    // Strict semantic analysis: treat undefined variables as errors
    bool strictSemantics = true;
};

} // namespace havel::compiler