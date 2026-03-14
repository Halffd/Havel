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

void registerAsyncModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI);

} // namespace modules
} // namespace havel
