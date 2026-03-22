#include "../../havel-lang/runtime/HostAPI.hpp"
/*
 * BrowserModule.hpp
 * 
 * Browser automation module for Havel language.
 */
#pragma once


namespace havel {

class Environment;

namespace modules {

void registerBrowserModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI);

} // namespace modules
} // namespace havel
