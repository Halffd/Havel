#pragma once
#include "havel-lang/compiler/vm/VMApi.hpp"
#include <string>

namespace havel::stdlib {

void registerLogModule(const compiler::VMApi &api);
void registerDebugModule(const compiler::VMApi &api);
void notifyRuntimeError(const std::string &msg);

} // namespace havel::stdlib
