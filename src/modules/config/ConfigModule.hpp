/*
* ConfigModule.hpp - Configuration module for bytecode VM
*/
#pragma once
#include "havel-lang/compiler/vm/VMApi.hpp"

namespace havel::modules {

void registerConfigModule(compiler::VMApi &api);
void autoLoadConfig(compiler::VMApi &api);

compiler::Value configGet(compiler::VMApi &api, const std::vector<compiler::Value> &args);
compiler::Value configSet(compiler::VMApi &api, const std::vector<compiler::Value> &args);
compiler::Value configSave(const std::vector<compiler::Value> &args);
compiler::Value configGetAll(compiler::VMApi &api, const std::vector<compiler::Value> &args);
compiler::Value configLoad(const std::vector<compiler::Value> &args);
compiler::Value configKeys(compiler::VMApi &api, const std::vector<compiler::Value> &args);

} // namespace havel::modules
