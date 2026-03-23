/*
 * PhysicsModule.hpp - Physics constants stdlib for VM (no host/service)
 * Pure VM implementation using BytecodeValue
 */
#pragma once
#include "../compiler/bytecode/VM.hpp"
#include "../compiler/bytecode/HostBridge.hpp"



namespace havel {
    class Environment;
    class HostContext;
    namespace modules { void registerPhysicsModule(Environment& env, HostContext&); }
}

namespace havel::stdlib {

// NEW: Register physics module with VM's host bridge (VM-native)
void registerModuleVM(compiler::HostBridge& bridge);

// Implementation in PhysicsModule.cpp

// Implementation of old registerPhysicsModule (placeholder)
inline void registerPhysicsModule(Environment& env, HostContext& ctx) {
    (void)env;
    (void)ctx;
}

} // namespace havel::stdlib
