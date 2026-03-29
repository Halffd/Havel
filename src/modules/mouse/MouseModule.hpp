/*
 * MouseModule.hpp - Mouse control module for bytecode VM
 */
#pragma once
#include "havel-lang/compiler/bytecode/VMApi.hpp"

namespace havel::modules {

// Register mouse module with VM
void registerMouseModule(compiler::VMApi &api);

} // namespace havel::modules
