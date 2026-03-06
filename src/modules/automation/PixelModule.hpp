/*
 * PixelModule.hpp
 * 
 * Pixel and image recognition module for Havel language.
 */
#pragma once

#include "../../host/HostContext.hpp"

namespace havel {

class Environment;

namespace modules {

void registerPixelModule(Environment& env, HostContext& ctx);

} // namespace modules
} // namespace havel
