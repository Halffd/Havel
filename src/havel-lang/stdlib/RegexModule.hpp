/*
 * RegexModule.hpp - Regular expression stdlib for VM (no host/service)
 * Pure VM implementation using BytecodeValue
 */
#pragma once
#include "../compiler/bytecode/VM.hpp"
#include "../compiler/bytecode/HostBridge.hpp"



#include <regex>

namespace havel {
    class Environment;
}

namespace havel::stdlib {

// OLD: Register with Environment (for AST interpreter) - DECLARATION ONLY
void registerRegexModule(Environment& env);

// NEW: Register regex module with VM's host bridge (VM-native)
void registerModuleVM(compiler::HostBridge& bridge);

// Implementation in RegexModule.cpp

// Implementation of old registerRegexModule (placeholder)
inline void registerRegexModule(Environment& env) {
    (void)env;
}

} // namespace havel::stdlib
