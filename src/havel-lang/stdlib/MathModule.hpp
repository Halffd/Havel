/*
 * MathModule.hpp - Math stdlib for VM (no host/service)
 * Pure VM implementation using BytecodeValue
 */
#pragma once
#include "../compiler/bytecode/VM.hpp"
#include "../compiler/bytecode/HostBridge.hpp"



#include <cmath>  // for std::ceil, std::floor, std::round, std::sin, etc.

namespace havel {
    class Environment;
}


namespace havel::stdlib {

// OLD: Register with Environment (for AST interpreter) - DECLARATION ONLY
void registerMathModule(Environment& env);

// NEW: Register math module with VM's host bridge (VM-native)
void registerMathModuleVM(compiler::HostBridge& bridge);

// Implementation in MathModule.cpp

// Implementation of old registerMathModule (placeholder)
inline void registerMathModule(Environment& env) {
    (void)env;
}

} // namespace havel::stdlib
