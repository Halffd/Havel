/*
 * StandardLibraryModule.hpp
 * 
 * Standard library module for Havel language.
 */
#pragma once

#include "../../host/HostContext.hpp"

namespace havel {

class Environment;

namespace modules {

void registerStandardLibraryModule(Environment& env, HostContext& ctx);

} // namespace modules
} // namespace havel
