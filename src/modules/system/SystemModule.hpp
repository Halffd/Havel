#pragma once
#include "../../havel-lang/runtime/Environment.hpp"
namespace havel { namespace modules { void registerSystemModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI); } }
