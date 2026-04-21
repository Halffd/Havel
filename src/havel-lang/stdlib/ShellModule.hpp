/* ShellModule.hpp - Shell operations stdlib for VM (VMApi) */
#pragma once
#include "havel-lang/compiler/vm/VMApi.hpp"

namespace havel { class Environment; }

namespace havel::stdlib {

void registerShellModule(compiler::VMApi &api);

inline void registerShellModule(Environment &env) { (void)env; }

} // namespace havel::stdlib
