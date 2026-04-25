#pragma once
#include "havel-lang/compiler/vm/VMApi.hpp"

namespace havel {
class Environment;
}

namespace havel::stdlib {

void registerBitModule(compiler::VMApi &api);

inline void registerBitModule(Environment &env) { (void)env; }

} // namespace havel::stdlib
