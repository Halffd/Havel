#pragma once
#include "havel-lang/compiler/vm/VMApi.hpp"

namespace havel::stdlib {

void registerLogModule(const compiler::VMApi &api);
void registerDebugModule(const compiler::VMApi &api);

} // namespace havel::stdlib
