/* SysModule.hpp - System info stdlib for VM (VMApi) */
#pragma once
#include "havel-lang/compiler/vm/VMApi.hpp"

namespace havel { class Environment; }

namespace havel::stdlib {

void registerSysModule(compiler::VMApi &api);

inline void registerSysModule(Environment &env) { (void)env; }

} // namespace havel::stdlib
