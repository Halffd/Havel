/*
 * ObjectModule.hpp - Object manipulation stdlib for VM with method chaining
 * Pure VM implementation using BytecodeValue
 */
#pragma once
#include "../compiler/bytecode/VM.hpp"
#include "../compiler/bytecode/HostBridge.hpp"



#include <algorithm>

namespace havel {
    class Environment;
}

namespace havel::stdlib {

// OLD: Register with Environment (for AST interpreter) - DECLARATION ONLY
void registerObjectModule(Environment& env);

// NEW: Register object module with VM's host bridge (VM-native)
void registerModuleVM(compiler::HostBridge& bridge);

// Implementation in ObjectModule.cpp

// Implementation of old registerObjectModule (placeholder)
inline void registerObjectModule(Environment& env) {
    (void)env;
}

} // namespace havel::stdlib
