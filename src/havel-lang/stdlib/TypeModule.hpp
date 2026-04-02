/*
 * TypeModule.hpp - Type conversion stdlib for VM (VMApi)
 * Pure VM implementation using VMApi
 */
#pragma once
#include "havel-lang/compiler/vm/VMApi.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace havel {
class Environment;
}

namespace havel::stdlib {

// OLD: Register with Environment (for AST interpreter) - DECLARATION ONLY
void registerTypeModule(Environment &env);

// NEW: Register type module with VMApi (stable API layer)
void registerTypeModule(compiler::VMApi &api);

// Implementation of old registerTypeModule (placeholder)
inline void registerTypeModule(Environment &env) { (void)env; }

} // namespace havel::stdlib
