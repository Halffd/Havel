/*
 * StringModule.hpp - String stdlib for VM with method chaining support
 * Pure VM implementation using BytecodeValue
 */
#pragma once
#include "../compiler/bytecode/VMApi.hpp"

#include <algorithm>
#include <cctype>
#include <cmath> // for std::floor
#include <sstream>

namespace havel {
class Environment;
}

namespace havel::stdlib {

// OLD: Register with Environment (for AST interpreter) - DECLARATION ONLY
void registerStringModule(Environment &env);

// NEW: Register string module with VMApi (stable API layer)
void registerStringModule(havel::compiler::VMApi &api);

// Implementation in StringModule.cpp

// Implementation of old registerStringModule (placeholder)
inline void registerStringModule(Environment &env) { (void)env; }

} // namespace havel::stdlib
