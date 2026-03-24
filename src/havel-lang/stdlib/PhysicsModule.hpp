/*
 * PhysicsModule.hpp - Physics constants stdlib for VM (VMApi)
 * Pure VM implementation using VMApi
 */
#pragma once
#include "../compiler/bytecode/VMApi.hpp"

namespace havel {
class Environment;
class HostContext;
namespace modules {
void registerPhysicsModule(Environment &env, HostContext &);
}
} // namespace havel

namespace havel::stdlib {

// NEW: Register physics module with VMApi (stable API layer)
void registerPhysicsModule(compiler::VMApi &api);

// Implementation of old registerPhysicsModule (placeholder)
inline void registerPhysicsModule(Environment &env, HostContext &ctx) {
  (void)env;
  (void)ctx;
}

} // namespace havel::stdlib
