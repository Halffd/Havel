#pragma once
#include "havel-lang/compiler/vm/VMApi.hpp"

namespace havel::stdlib {

void registerBytecodeBuilderModule(const compiler::VMApi &api);

} // namespace havel::stdlib
