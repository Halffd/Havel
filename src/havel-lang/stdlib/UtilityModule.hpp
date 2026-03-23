/*
 * UtilityModule.hpp - Utility stdlib for VM (no host/service)
 * Pure VM implementation using BytecodeValue
 */
#pragma once
#include "../compiler/bytecode/VM.hpp"
#include "../compiler/bytecode/HostBridge.hpp"



namespace havel {
    class Environment;
}


#include <algorithm>

namespace havel::stdlib {

// OLD: Register with Environment (for AST interpreter) - DECLARATION ONLY
void registerUtilityModule(Environment& env);

// NEW: Register utility module with VM's host bridge (VM-native)
#include "../compiler/bytecode/VM.hpp"
void registerModule(compiler::HostBridge& bridge);

// Implementation in UtilityModule.cpp

// Implementation of old registerUtilityModule (placeholder)
inline void registerUtilityModule(Environment& env) {
    (void)env;
}

} // namespace havel::stdlib
