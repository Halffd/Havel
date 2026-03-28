/*
 * ConfigModule.hpp - Configuration module for bytecode VM
 */
#pragma once
#include "havel-lang/compiler/bytecode/VMApi.hpp"

namespace havel::modules {

// Register config module with VM (config.get, config.set, config.save, etc.)
void registerConfigModule(compiler::VMApi &api);

// Auto-load config from file to global conf object
void autoLoadConfig(compiler::VMApi &api);

} // namespace havel::modules
