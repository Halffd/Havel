#pragma once
#include "../../havel-lang/runtime/Environment.hpp"
namespace havel { namespace modules { void registerRuntimeModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI); } }
