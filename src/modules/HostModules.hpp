// HostModules.hpp
// Host module registration

#pragma once
#include "havel-lang/runtime/Modules.hpp"
#include "../host/ServiceRegistry.hpp"

namespace havel {

class IHostAPI;

void initializeServiceRegistry(std::shared_ptr<IHostAPI> hostAPI,
							   const host::ServiceFilter& includes = {},
							   const host::ServiceFilter& excludes = {});

void declareAllServices();

HostContext createHostContext(std::shared_ptr<IHostAPI> hostAPI);

void registerHostModules(ModuleLoader& loader);

void loadHostModules(Environment& env, ModuleLoader& loader, std::shared_ptr<IHostAPI> hostAPI);

} // namespace havel
