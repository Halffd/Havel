// StdLibModules.hpp
// Standard library module registration

#pragma once
#include "ModuleLoader.hpp"

namespace havel {

// Register all standard library modules
void registerStdLibModules(ModuleLoader& loader);

// Load all stdlib modules into environment
void loadStdLibModules(Environment& env, ModuleLoader& loader);

} // namespace havel
