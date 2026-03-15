// ModuleLoader.hpp
// Module loading system
// Simple, explicit, no magic

#pragma once
#include <string>
#include <memory>
namespace havel {

class Environment;
class IHostAPI;

namespace modules {

// Define alias to module member (supports dotted paths like "mouse.scroll")
bool defineHostAlias(Environment& env, const std::string& alias,
                     const std::string& moduleName,
                     const std::string& memberPath);

// Define alias to existing global name
bool defineGlobalAlias(Environment& env, const std::string& alias,
                       const std::string& sourceName);

// Load all modules (stdlib + host)
void loadAllModules(Environment& env, std::shared_ptr<IHostAPI> hostAPI);

} // namespace modules
} // namespace havel
