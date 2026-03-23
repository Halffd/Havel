/*
 * ProcessModule.hpp - Process management stdlib for VM (no host/service)
 * Pure VM implementation using BytecodeValue
 */
#pragma once
#include "../compiler/bytecode/VM.hpp"
#include "../compiler/bytecode/HostBridge.hpp"



#include "core/process/ProcessManager.hpp"
#include <cstdlib>
#include <sstream>

namespace havel {
    class Environment;
}

namespace havel::stdlib {

// OLD: Register with Environment (for AST interpreter) - DECLARATION ONLY
void registerProcessModule(Environment& env);

// NEW: Register process module with VM's host bridge (VM-native)
void registerModuleVM(compiler::HostBridge& bridge);

// Implementation in ProcessModule.cpp

// Implementation of old registerProcessModule (placeholder)
inline void registerProcessModule(Environment& env) {
    (void)env;
}

} // namespace havel::stdlib
