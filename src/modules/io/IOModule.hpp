#include "../../havel-lang/runtime/HostAPI.hpp"
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

void registerIOModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI);

} // namespace modules
} // namespace havel
