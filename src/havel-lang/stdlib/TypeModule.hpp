/*
 * TypeModule.hpp - Type conversion stdlib for VM (no host/service)
 * Pure VM implementation using BytecodeValue
 */
#pragma once
#include "../compiler/bytecode/VM.hpp"
#include "../compiler/bytecode/HostBridge.hpp"



namespace havel {
    class Environment;
}


#include <algorithm>
#include <cmath>
#include <sstream>

namespace havel::stdlib {

// OLD: Register with Environment (for AST interpreter) - DECLARATION ONLY
void registerTypeModule(Environment& env);

// NEW: Register type module with VM's host bridge (VM-native)
void registerModuleVM(compiler::HostBridge& bridge);

// Implementation in TypeModule.cpp

// Implementation of old registerTypeModule (placeholder)
inline void registerTypeModule(Environment& env) {
    (void)env;
}

} // namespace havel::stdlib
