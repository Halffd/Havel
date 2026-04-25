#pragma once
#include "havel-lang/compiler/vm/VMApi.hpp"

namespace havel {
class Environment;
}

namespace havel::stdlib {

void registerPackModule(compiler::VMApi &api);

inline void registerPackModule(Environment &env) { (void)env; }

} // namespace havel::stdlib
