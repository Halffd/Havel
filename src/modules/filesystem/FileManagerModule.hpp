#include "../../havel-lang/runtime/HostAPI.hpp"
/*
 * FileManagerModule.hpp
 * 
 * Advanced file operations module for Havel language.
 */
#pragma once

#include "../../host/HostContext.hpp"

namespace havel {

class Environment;

namespace modules {

void registerFileManagerModule(Environment& env, IHostAPI* hostAPI);

} // namespace modules
} // namespace havel
