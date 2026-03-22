#include "../../havel-lang/runtime/HostAPI.hpp"
/*
 * PixelModule.hpp
 * 
 * Pixel and image recognition module for Havel language.
 */
#pragma once


namespace havel {

class Environment;

namespace modules {

void registerPixelModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI);

} // namespace modules
} // namespace havel
