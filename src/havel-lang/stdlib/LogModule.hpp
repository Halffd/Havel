#pragma once
#include "havel-lang/compiler/vm/VMApi.hpp"

namespace havel::stdlib {

void registerLogModule(compiler::VMApi &api);
void registerDebugModule(compiler::VMApi &api);

} // namespace havel::stdlib
