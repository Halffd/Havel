/*
 * WindowMonitorModule.hpp - Window monitoring and manipulation module for bytecode VM
 * Provides dynamic window variables: title, class, exe, pid
 * And window manipulation host functions
 */
#pragma once
#include "havel-lang/compiler/vm/VMApi.hpp"

namespace havel { class WindowMonitor; }

namespace havel::modules {

void registerWindowMonitorModule(const compiler::VMApi &api);

void setupDynamicWindowGlobals(const compiler::VMApi &api, WindowMonitor *monitor);

} // namespace havel::modules
