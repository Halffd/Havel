/*
 * HelpModule.hpp - Help system for bytecode VM
 */
#pragma once
#include "havel-lang/compiler/bytecode/VMApi.hpp"

namespace havel::modules {

// Register help module with VM
void registerHelpModule(compiler::VMApi &api);

} // namespace havel::modules
