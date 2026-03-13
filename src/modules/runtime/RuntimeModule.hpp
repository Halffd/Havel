#include "../../havel-lang/runtime/HostAPI.hpp"
/*
 * RuntimeModule.hpp
 * 
 * Runtime utilities module for Havel language.
 * Provides app control, debug utilities, and runOnce functionality.
 */
#pragma once

#include "../../host/HostContext.hpp"

namespace havel {

class Environment;
class Interpreter;

namespace modules {

void registerRuntimeModule(Environment& env, Interpreter* interpreter);

} // namespace modules
} // namespace havel
