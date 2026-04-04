/*
 * ScreenshotModule.hpp
 * 
 * Screenshot module for Havel language - bytecode VM implementation.
 */
#pragma once

#include "havel-lang/compiler/vm/VMApi.hpp"

namespace havel::modules {

void registerScreenshotModule(compiler::VMApi &api);

} // namespace havel::modules
