/*
 * MouseModule.hpp - Mouse control module for bytecode VM
 */
#pragma once
#include "havel-lang/compiler/vm/VMApi.hpp"

namespace havel::modules {

// Register mouse module with VM
void registerMouseModule(compiler::VMApi &api);

} // namespace havel::modules
