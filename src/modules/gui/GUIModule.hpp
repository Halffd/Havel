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

void registerGUIModule(Environment& env, HostContext& ctx);

} // namespace modules
} // namespace havel
