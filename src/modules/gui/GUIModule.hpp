#include "../../havel-lang/runtime/HostAPI.hpp"
/*
 * GUIModule.hpp
 * 
 * GUI dialogs module for Havel language.
 */
#pragma once

#include "../../host/HostContext.hpp"

namespace havel {

class Environment;

namespace modules {

void registerGUIModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI);

} // namespace modules
} // namespace havel
