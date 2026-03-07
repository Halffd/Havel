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

void registerFileManagerModule(Environment& env, HostContext& ctx);

} // namespace modules
} // namespace havel
