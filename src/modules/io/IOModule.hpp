/*
 * IOModule.hpp
 *
 * IO control module for Havel language.
 */
#pragma once

#include "../../host/HostContext.hpp"
#include "core/io/KeyTap.hpp"

namespace havel {

class Environment;
class IO;

namespace modules {

void registerIOModule(Environment& env, HostContext& ctx);

} // namespace modules
} // namespace havel
