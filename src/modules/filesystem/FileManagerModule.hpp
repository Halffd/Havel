#include "../../havel-lang/runtime/HostAPI.hpp"
/*
 * FileManagerModule.hpp
 * 
 * Advanced file operations module for Havel language.
 */
#pragma once


namespace havel {

class Environment;

namespace modules {

void registerFileManagerModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI);

} // namespace modules
} // namespace havel
