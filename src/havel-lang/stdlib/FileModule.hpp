/*
 * FileModule.hpp - File I/O stdlib for VM (no host/service)
 * Pure VM implementation using BytecodeValue
 */
#pragma once
#include "../compiler/bytecode/VM.hpp"
#include "../compiler/bytecode/HostBridge.hpp"



#include <filesystem>
#include <fstream>
#include <sstream>

namespace havel {
    class Environment;
}

namespace havel::stdlib {

// OLD: Register with Environment (for AST interpreter) - DECLARATION ONLY
void registerFileModule(Environment& env);

// NEW: Register file module with VM's host bridge (VM-native)
void registerModuleVM(compiler::HostBridge& bridge);

// Implementation in FileModule.cpp

// Implementation of old registerFileModule (placeholder)
inline void registerFileModule(Environment& env) {
    (void)env;
}

} // namespace havel::stdlib
