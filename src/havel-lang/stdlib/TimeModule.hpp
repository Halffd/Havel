/*
 * TimeModule.hpp - Time and date stdlib for VM (no host/service)
 * Pure VM implementation using BytecodeValue
 */
#pragma once
#include "../compiler/bytecode/VM.hpp"
#include "../compiler/bytecode/HostBridge.hpp"



#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <thread>

namespace havel {
    class Environment;
}

namespace havel::stdlib {

// OLD: Register with Environment (for AST interpreter) - DECLARATION ONLY
void registerTimeModule(Environment& env);

// NEW: Register time module with VM's host bridge (VM-native)
void registerModuleVM(compiler::HostBridge& bridge);

// Implementation in TimeModule.cpp

// Implementation of old registerTimeModule (placeholder)
inline void registerTimeModule(Environment& env) {
    (void)env;
}

} // namespace havel::stdlib
