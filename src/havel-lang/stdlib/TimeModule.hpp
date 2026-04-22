/*
 * TimeModule.hpp - Time and date stdlib for VM (VMApi)
 * Pure VM implementation using VMApi
 */
#pragma once
#include "havel-lang/compiler/vm/VMApi.hpp"

#include <chrono>
#include <ctime>

namespace havel::stdlib {

// Register time module with VMApi
void registerTimeModule(compiler::VMApi &api);

} // namespace havel::stdlib
