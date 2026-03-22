#include "../../havel-lang/runtime/HostAPI.hpp"
/*
 * AutomationModule.hpp
 * 
 * Automation module for Havel language.
 */
#pragma once


namespace havel {

class Environment;

namespace modules {

void registerAutomationModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI);

} // namespace modules
} // namespace havel
