// HostModules.hpp
// Host module registration

#pragma once
#include "../havel-lang/runtime/ModuleLoader.hpp"

namespace havel {

// Register all host modules
void registerHostModules(ModuleLoader& loader);

// Load all host modules into environment
void loadHostModules(Environment& env, ModuleLoader& loader, IHostAPI* hostAPI);

} // namespace havel
