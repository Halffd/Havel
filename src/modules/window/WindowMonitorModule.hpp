/*
 * WindowMonitorModule.hpp - Window monitoring module for bytecode VM
 * Provides dynamic window variables: title, class, exe, pid
 */
#pragma once
#include "havel-lang/compiler/bytecode/VMApi.hpp"

namespace havel::modules {

// Register window monitor module with VM
void registerWindowMonitorModule(compiler::VMApi &api);

// Auto-setup dynamic window globals (title, class, exe, pid)
void setupDynamicWindowGlobals(compiler::VMApi &api);

} // namespace havel::modules
