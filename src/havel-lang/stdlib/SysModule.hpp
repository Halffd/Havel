#pragma once
#include "havel-lang/compiler/vm/VMApi.hpp"

#include <chrono>
#include <ctime>

namespace havel {
class Environment;
}

namespace havel::stdlib {

void registerSysModule(Environment &env);
void registerSysModule(compiler::VMApi &api);
inline void registerSysModule(Environment &env) { (void)env; }

} // namespace havel::stdlib