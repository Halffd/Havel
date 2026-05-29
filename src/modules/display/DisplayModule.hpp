/*
 * DisplayModule.hpp - Display management module for bytecode VM
 * Provides: display.monitors, display.primary, display.count, display.area,
 *           display.isX11, display.isWayland, display.isWindows, display.protocol,
 *           display.wm, display.displayNum, display.resolutions,
 *           display.monitorAt, display.monitorByName, display.primaryName
 */
#pragma once
#include "havel-lang/compiler/vm/VMApi.hpp"

namespace havel::modules {

void registerDisplayModule(const compiler::VMApi &api);

} // namespace havel::modules
