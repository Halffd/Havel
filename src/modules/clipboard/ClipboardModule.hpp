#include "../../havel-lang/runtime/HostAPI.hpp"
/*
 * ClipboardModule.hpp
 * 
 * Clipboard module for Havel language.
 */
#pragma once

#include "../../host/HostContext.hpp"

namespace havel {

class Environment;

namespace modules {

void registerClipboardModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI);

} // namespace modules
} // namespace havel
