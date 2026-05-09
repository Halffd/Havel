/*
 * TimerModule.hpp - Timer stdlib for VM (VMApi)
 *
 * Provides timer.setTimeout, timer.setInterval, timer.clear,
 * timer.activeCount, timer.clearAll namespace.
 *
 * Delegates to VM's existing interval.start/stop and timeout.start/cancel
 * host functions for actual execution.
 */
#pragma once

#include "havel-lang/compiler/vm/VMApi.hpp"

namespace havel::stdlib {

void registerTimerModule(const compiler::VMApi &api);

} // namespace havel::stdlib
