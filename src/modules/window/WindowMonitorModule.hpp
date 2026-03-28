/*
 * WindowMonitorModule.hpp - Window monitoring module for bytecode VM
 * Provides dynamic window variables: title, class, exe, pid
 */
#pragma once
#include "havel-lang/compiler/bytecode/VMApi.hpp"

namespace havel { class WindowMonitor; }

namespace havel::modules {

// Register window monitor module with VM
void registerWindowMonitorModule(compiler::VMApi &api);

// Setup dynamic window globals with existing WindowMonitor
void setupDynamicWindowGlobals(compiler::VMApi &api, WindowMonitor *monitor);

} // namespace havel::modules
