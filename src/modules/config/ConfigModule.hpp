/*
* ConfigModule.hpp - Configuration module for bytecode VM
*/
#pragma once
#include "havel-lang/compiler/vm/VMApi.hpp"

namespace havel::modules {

void registerConfigModule(const compiler::VMApi &api);
void autoLoadConfig(const compiler::VMApi &api);

compiler::Value configGet(const compiler::VMApi &api, const std::vector<compiler::Value> &args);
compiler::Value configSet(const compiler::VMApi &api, const std::vector<compiler::Value> &args);
compiler::Value configSave(const std::vector<compiler::Value> &args);
compiler::Value configGetAll(const compiler::VMApi &api, const std::vector<compiler::Value> &args);
compiler::Value configLoad(const std::vector<compiler::Value> &args);
compiler::Value configKeys(const compiler::VMApi &api, const std::vector<compiler::Value> &args);

} // namespace havel::modules
