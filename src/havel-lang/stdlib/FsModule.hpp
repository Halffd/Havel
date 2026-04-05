/*
 * FsModule.hpp - File system stdlib for VM (VMApi)
 * Pure VM implementation using VMApi
 */
#pragma once
#include "havel-lang/compiler/vm/VMApi.hpp"

namespace havel::stdlib {

// Register fs module with VMApi (stable API layer)
void registerFsModule(compiler::VMApi &api);

} // namespace havel::stdlib
