/*
 * ConfigModule.hpp - Configuration module for bytecode VM
 */
#pragma once
#include "havel-lang/compiler/vm/VMApi.hpp"

namespace havel::modules {

// Register config module with VM (config.get, config.set, config.save, etc.)
void registerConfigModule(compiler::VMApi &api);

// Auto-load config from file to global conf object
void autoLoadConfig(compiler::VMApi &api);

// Config functions (exported for direct registration)
compiler::Value configGet(const std::vector<compiler::Value> &args);
compiler::Value configSet(const std::vector<compiler::Value> &args);
compiler::Value configSave(const std::vector<compiler::Value> &args);
compiler::Value configGetAll(compiler::VMApi &api, const std::vector<compiler::Value> &args);
compiler::Value configLoad(const std::vector<compiler::Value> &args);

} // namespace havel::modules
