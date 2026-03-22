#include "../../havel-lang/runtime/HostAPI.hpp"
/*
 * AudioModule.hpp
 * 
 * Audio management module for Havel language.
 */
#pragma once


namespace havel {

class Environment;

namespace modules {

void registerAudioModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI);

} // namespace modules
} // namespace havel
