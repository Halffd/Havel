/*
 * MathModule.hpp - Math stdlib for VM (no host/service)
 * Pure VM implementation using BytecodeValue
 */
#pragma once
#include "../compiler/bytecode/VMApi.hpp"

#include <cmath> // for std::ceil, std::floor, std::round, std::sin, etc.

namespace havel {
class Environment;
}

namespace havel::stdlib {

// OLD: Register with Environment (for AST interpreter) - DECLARATION ONLY
void registerMathModule(Environment &env);

// NEW: Register math module with VMApi (stable API layer)
void registerModule(havel::compiler::VMApi &api);

// Implementation in MathModule.cpp

// Implementation of old registerMathModule (placeholder)
inline void registerMathModule(Environment &env) { (void)env; }

} // namespace havel::stdlib
