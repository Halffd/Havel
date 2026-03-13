#include "../../havel-lang/runtime/HostAPI.hpp"
/*
 * AsyncModule.hpp
 * 
 * Async/concurrency module for Havel language.
 */
#pragma once

#include "../../host/HostContext.hpp"

namespace havel {

class Environment;

namespace modules {

void registerAsyncModule(Environment& env, IHostAPI* hostAPI);

} // namespace modules
} // namespace havel
