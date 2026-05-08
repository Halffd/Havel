#pragma once
#include "havel-lang/compiler/vm/VMApi.hpp"

#include <chrono>
#include <ctime>

namespace havel::stdlib {

void registerSysModule(compiler::VMApi &api);

} // namespace havel::stdlib