/*
 * FileModule.hpp - File I/O stdlib for VM (VMApi)
 * Pure VM implementation using VMApi
 */
#pragma once
#include "../compiler/bytecode/VMApi.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace havel {
class Environment;
}

namespace havel::stdlib {

// OLD: Register with Environment (for AST interpreter) - DECLARATION ONLY
void registerFileModule(Environment &env);

// NEW: Register file module with VMApi (stable API layer)
void registerFileModule(compiler::VMApi &api);

// Implementation of old registerFileModule (placeholder)
inline void registerFileModule(Environment &env) { (void)env; }

} // namespace havel::stdlib
