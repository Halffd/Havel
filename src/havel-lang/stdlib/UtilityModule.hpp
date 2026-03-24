/*
 * UtilityModule.hpp - Utility stdlib for VM (VMApi)
 * Pure VM implementation using VMApi
 */
#pragma once
#include "../compiler/bytecode/VMApi.hpp"

#include <algorithm>

namespace havel {
class Environment;
}

namespace havel::stdlib {

// OLD: Register with Environment (for AST interpreter) - DECLARATION ONLY
void registerUtilityModule(Environment &env);

// NEW: Register utility module with VMApi (stable API layer)
void registerUtilityModule(compiler::VMApi &api);

// Implementation of old registerUtilityModule (placeholder)
inline void registerUtilityModule(Environment &env) { (void)env; }

} // namespace havel::stdlib
