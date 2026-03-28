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

// Config functions (exported for direct registration)
compiler::BytecodeValue configGet(const std::vector<compiler::BytecodeValue> &args);
compiler::BytecodeValue configSet(const std::vector<compiler::BytecodeValue> &args);
compiler::BytecodeValue configSave(const std::vector<compiler::BytecodeValue> &args);
compiler::BytecodeValue configGetAll(compiler::VMApi &api, const std::vector<compiler::BytecodeValue> &args);
compiler::BytecodeValue configLoad(const std::vector<compiler::BytecodeValue> &args);

} // namespace havel::modules
