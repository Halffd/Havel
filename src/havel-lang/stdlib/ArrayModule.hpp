/*
 * ArrayModule.hpp - Array stdlib for VM (no host/service)
 * Pure VM implementation using BytecodeValue
 */
#pragma once
#include "../compiler/bytecode/VM.hpp"
#include "../compiler/bytecode/HostBridge.hpp"



#include <algorithm>
#include <cmath>

namespace havel {
    class Environment;
    class Interpreter;
}

namespace havel::stdlib {

// OLD: Register with Environment (for AST interpreter) - DECLARATION ONLY
void registerArrayModule(Environment& env, Interpreter* interpreter);

// NEW: Register array module with VM's host bridge (VM-native)
void registerModuleVM(compiler::HostBridge& bridge);

// Implementation in ArrayModule.cpp

// Implementation of old registerArrayModule (placeholder)
inline void registerArrayModule(Environment& env, Interpreter* interpreter) {
    (void)env;
    (void)interpreter;
}

} // namespace havel::stdlib
