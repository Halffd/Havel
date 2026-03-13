// ExampleModule.hpp
// Example of proper module structure
// Use this as a template for new modules

#pragma once
#include "../havel-lang/runtime/Environment.hpp"
#include "../havel-lang/runtime/HostAPI.hpp"

namespace havel::modules {

/**
 * Example standard library module
 * 
 * Usage in script:
 *   use example
 *   example.hello()
 */
void registerExampleModule(Environment& env);

/**
 * Example host module
 * 
 * Usage in script:
 *   use system
 *   system.getInfo()
 */
void registerSystemModule(Environment& env, IHostAPI* hostAPI);

} // namespace havel::modules
