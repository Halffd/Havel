/*
 * ClipboardMgrModule.hpp
 * 
 * GUI Clipboard Manager module for Havel language.
 * Disabled by default, can be enabled from script.
 */
#pragma once

#include "havel-lang/compiler/vm/VMApi.hpp"

namespace havel::modules {

void registerClipboardMgrModule(compiler::VMApi &api);

} // namespace havel::modules
