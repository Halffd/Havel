/*
 * StringModule.hpp - String stdlib for VM with method chaining support
 * Pure VM implementation using BytecodeValue
 */
#pragma once
#include "../compiler/bytecode/VM.hpp"
#include "../compiler/bytecode/HostBridge.hpp"



#include <algorithm>
#include <cctype>
#include <cmath>    // for std::floor
#include <sstream>

namespace havel {
    class Environment;
}

namespace havel::stdlib {

// OLD: Register with Environment (for AST interpreter) - DECLARATION ONLY
void registerStringModule(Environment& env);

// NEW: Register string module with VM's host bridge (VM-native)
void registerModuleVM(compiler::HostBridge& bridge);

// Implementation in StringModule.cpp

// Implementation of old registerStringModule (placeholder)
inline void registerStringModule(Environment& env) {
    (void)env;
}

} // namespace havel::stdlib
