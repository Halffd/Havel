// StdLibModules.cpp
// Register all standard library modules with VM

#include "../compiler/bytecode/HostBridge.hpp"
#include "../compiler/bytecode/VMApi.hpp"

namespace havel::stdlib {
void registerModule(compiler::VMApi &api);
}

namespace havel {

void registerStdLibWithVM(compiler::HostBridge &bridge) {
  // Create VMApi from bridge's VM
  compiler::VMApi api{*bridge.context().vm};

  // Register only working stdlib modules with VMApi
  // TODO: Update remaining modules to use VMApi
  stdlib::registerModule(api);

  // Add math constants to host_global_names so compiler knows about them
  bridge.options().host_global_names.insert("PI");
  bridge.options().host_global_names.insert("E");
  bridge.options().host_global_names.insert("math");

  // Temporarily disabled modules that need VMApi migration:
  // - ArrayModule (direct VM access)
  // - FileModule (direct VM access)
  // - ObjectModule (direct VM access)
  // - PhysicsModule (direct VM access)
  // - ProcessModule (direct VM access)
  // - RegexModule (direct VM access)
  // - StringModule (direct VM access)
  // - TimeModule (direct VM access)
  // - TypeModule (direct VM access)
  // - UtilityModule (direct VM access)
}

} // namespace havel
