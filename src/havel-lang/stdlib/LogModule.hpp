/*
 * LogModule.hpp - Logging stdlib for VM
 * Provides log.info, log.error, log.warn, log.debug, log.critical
 * Also log.get(), log.set(file), log.history(), log.log(file, ...msg)
 */
#pragma once
#include "havel-lang/compiler/vm/VMApi.hpp"

namespace havel {
class Environment;
}

namespace havel::stdlib {

void registerLogModule(havel::compiler::VMApi &api);

inline void registerLogModule(Environment &env) { (void)env; }

} // namespace havel::stdlib
