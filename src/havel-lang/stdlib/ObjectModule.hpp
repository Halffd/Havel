/*
 * ObjectModule.hpp - Object manipulation stdlib for VM with method chaining
 * Pure VM implementation using BytecodeValue
 */
#pragma once
#include "havel-lang/compiler/vm/VMApi.hpp"

#include <algorithm>

namespace havel {
class Environment;
}

namespace havel::stdlib {

// OLD: Register with Environment (for AST interpreter) - DECLARATION ONLY
void registerObjectModule(Environment &env);

// NEW: Register object module with VMApi (stable API layer)
void registerObjectModule(havel::compiler::VMApi &api);

// Implementation in ObjectModule.cpp

// Implementation of old registerObjectModule (placeholder)
inline void registerObjectModule(Environment &env) { (void)env; }

} // namespace havel::stdlib
