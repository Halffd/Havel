/*
 * AutomationModule.hpp
 * 
 * Automation module for Havel language.
 */
#pragma once

#include "../../host/HostContext.hpp"

namespace havel {

class Environment;

namespace modules {

void registerAutomationModule(Environment& env, HostContext& ctx);

} // namespace modules
} // namespace havel
