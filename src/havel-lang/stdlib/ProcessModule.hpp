/*
 * ProcessModule.hpp - Process management stdlib for VM (VMApi)
 * Pure VM implementation using VMApi
 */
#pragma once
#include "../compiler/bytecode/VMApi.hpp"

#include <cstdlib>
#include <sstream>

namespace havel {
class Environment;
}

namespace havel::stdlib {

// OLD: Register with Environment (for AST interpreter) - DECLARATION ONLY
void registerProcessModule(Environment &env);

// NEW: Register process module with VMApi (stable API layer)
void registerProcessModule(compiler::VMApi &api);

// Implementation of old registerProcessModule (placeholder)
inline void registerProcessModule(Environment &env) { (void)env; }

} // namespace havel::stdlib
