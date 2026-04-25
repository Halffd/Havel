#pragma once
#include "havel-lang/compiler/vm/VMApi.hpp"

namespace havel {
class Environment;
}

namespace havel::stdlib {

void registerFormatModule(compiler::VMApi &api);

inline void registerFormatModule(Environment &env) { (void)env; }

} // namespace havel::stdlib
