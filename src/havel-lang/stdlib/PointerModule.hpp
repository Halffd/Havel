#pragma once
#include "havel-lang/compiler/vm/VMApi.hpp"

namespace havel {
class Environment;
}

namespace havel::stdlib {

void registerPointerModule(compiler::VMApi &api);

inline void registerPointerModule(Environment &env) { (void)env; }

} // namespace havel::stdlib
