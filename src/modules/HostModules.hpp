// HostModules.hpp
// Host module registration

#pragma once
#include "../havel-lang/runtime/ModuleLoader.hpp"
#include "../havel-lang/compiler/bytecode/HostBridge.hpp"

namespace havel {

class IHostAPI;

// Initialize service registry with all services
void initializeServiceRegistry(std::shared_ptr<IHostAPI> hostAPI);

// Create HostContext with injected dependencies
HostContext createHostContext(std::shared_ptr<IHostAPI> hostAPI);

// Register all host modules (called once at startup)
void registerHostModules(ModuleLoader& loader);

// Load all host modules into environment (called per environment)
void loadHostModules(Environment& env, ModuleLoader& loader, std::shared_ptr<IHostAPI> hostAPI);

} // namespace havel
