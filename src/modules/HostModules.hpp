// HostModules.hpp
// Host module registration

#pragma once
#include "../havel-lang/runtime/ModuleLoader.hpp"
#include "../havel-lang/compiler/bytecode/HostBridge.hpp"

namespace havel {

class IHostAPI;

// Initialize service registry with all services
void initializeServiceRegistry(std::shared_ptr<IHostAPI> hostAPI);

// Create HostBridgeDependencies with service registry
compiler::HostBridgeDependencies createHostBridgeDependencies(
    std::shared_ptr<IHostAPI> hostAPI,
    compiler::VM* vm);

// Register all host modules (called once at startup)
void registerHostModules(ModuleLoader& loader);

// Load all host modules into environment (called per environment)
void loadHostModules(Environment& env, ModuleLoader& loader, std::shared_ptr<IHostAPI> hostAPI);

} // namespace havel
