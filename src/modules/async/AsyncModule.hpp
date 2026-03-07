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

void registerAsyncModule(Environment& env, HostContext& ctx);

} // namespace modules
} // namespace havel
