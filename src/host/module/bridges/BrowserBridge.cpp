// BrowserBridge implementation — extracted from ModularHostBridges.cpp when
// the file was split per-domain. No behavior change.

#include "../ModularHostBridges.hpp"
#include "BridgesInternal.hpp"

namespace havel::compiler {
void BrowserBridge::install(PipelineOptions &options) {
  // Browser functionality is now provided by stdlib::registerBrowserModule
  // in StdLibModules.cpp. No host_functions registered here.
}

// ============================================================================
// ToolsBridge Implementation
// ============================================================================

namespace {
::havel::host::TextChunkerService g_textChunker;
}

} // namespace havel::compiler
