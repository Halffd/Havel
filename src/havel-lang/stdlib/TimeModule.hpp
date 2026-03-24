/*
 * TimeModule.hpp - Time and date stdlib for VM (VMApi)
 * Pure VM implementation using VMApi
 */
#pragma once
#include "../compiler/bytecode/VMApi.hpp"

#include <chrono>
#include <ctime>

namespace havel {
class Environment;
}

namespace havel::stdlib {

// OLD: Register with Environment (for AST interpreter) - DECLARATION ONLY
void registerTimeModule(Environment &env);

// NEW: Register time module with VMApi (stable API layer)
void registerTimeModule(compiler::VMApi &api);

// Implementation of old registerTimeModule (placeholder)
inline void registerTimeModule(Environment &env) { (void)env; }

} // namespace havel::stdlib
