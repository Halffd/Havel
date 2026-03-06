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

void registerClipboardModule(Environment& env, HostContext& ctx);

} // namespace modules
} // namespace havel
