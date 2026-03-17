// StdLibModules.hpp
// Standard library module registration
// Simple, explicit, no magic

#pragma once
#include "ModuleLoader.hpp"

namespace havel {

class Interpreter;

// Register all standard library modules
void registerStdLibModules(ModuleLoader &loader);

// Load all stdlib modules into environment
void loadStdLibModules(Environment &env, ModuleLoader &loader,
                       Interpreter *interpreter = nullptr);

} // namespace havel
