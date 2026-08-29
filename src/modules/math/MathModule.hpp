/* MathModule.hpp - Native vectorized math module */
#pragma once

#include "c/ModulePlugin.h"
#include "compiler/vm/VMApi.hpp"

namespace havel::stdlib {

void registerMathNativeModule(const havel::compiler::VMApi &api);

} // namespace havel::stdlib
