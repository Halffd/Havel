// NetworkBridge implementation — extracted from ModularHostBridges.cpp when
// the file was split per-domain. No behavior change.

#include "../ModularHostBridges.hpp"
#include "BridgesInternal.hpp"

namespace havel::compiler {
void NetworkBridge::install(PipelineOptions &options) {
  // HTTP functionality is now provided by stdlib::registerHttpModule
  // in StdLibModules.cpp. No host_functions registered here.
}



// ============================================================================
// AudioBridge Implementation
// ============================================================================

} // namespace havel::compiler
