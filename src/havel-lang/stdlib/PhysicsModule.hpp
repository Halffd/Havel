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
inline void registerPhysicsModuleVM(compiler::HostBridgeRegistry& registry) {
    auto& vm = registry.vm();
    
    // Create physics object with constants
    auto physicsObj = vm.createHostObject();
    
    // Speed of light (m/s)
    vm.setHostObjectField(physicsObj, "c", compiler::BytecodeValue(299792458.0));
    
    // Gravitational constant (N⋅m²/kg²)
    vm.setHostObjectField(physicsObj, "G", compiler::BytecodeValue(6.67430e-11));
    
    // Elementary charge (C)
    vm.setHostObjectField(physicsObj, "e", compiler::BytecodeValue(1.602176634e-19));
    
    // Electron mass (kg)
    vm.setHostObjectField(physicsObj, "me", compiler::BytecodeValue(9.10938356e-31));
    
    // Proton mass (kg)
    vm.setHostObjectField(physicsObj, "mp", compiler::BytecodeValue(1.67262192369e-27));
    
    // Planck constant (J⋅s)
    vm.setHostObjectField(physicsObj, "h", compiler::BytecodeValue(6.62607015e-34));
    
    // Avogadro constant (mol⁻¹)
    vm.setHostObjectField(physicsObj, "NA", compiler::BytecodeValue(6.02214076e23));
    
    // Boltzmann constant (J/K)
    vm.setHostObjectField(physicsObj, "k", compiler::BytecodeValue(1.380649e-23));
    
    // Permittivity of free space (F/m)
    vm.setHostObjectField(physicsObj, "epsilon0", compiler::BytecodeValue(8.854187817e-12));
    
    // Permeability of free space (H/m)
    vm.setHostObjectField(physicsObj, "mu0", compiler::BytecodeValue(1.25663706212e-6));
    
    // Fine structure constant
    vm.setHostObjectField(physicsObj, "alpha", compiler::BytecodeValue(7.2973525693e-3));
    
    // Rydberg constant (J)
    vm.setHostObjectField(physicsObj, "Rinf", compiler::BytecodeValue(10973731.56816));
    
    // Stefan-Boltzmann constant (W⋅m⁻²⋅K⁻⁴)
    vm.setHostObjectField(physicsObj, "sigma", compiler::BytecodeValue(5.670374419e-8));
    
    // Electron volt (J)
    vm.setHostObjectField(physicsObj, "eV", compiler::BytecodeValue(1.602176634e-19));
    
    // Atomic mass unit (kg)
    vm.setHostObjectField(physicsObj, "u", compiler::BytecodeValue(1.66053906660e-27));
    
    // Bohr radius (m)
    vm.setHostObjectField(physicsObj, "a0", compiler::BytecodeValue(5.29177210903e-11));
    
    // Classical electron radius (m)
    vm.setHostObjectField(physicsObj, "re", compiler::BytecodeValue(2.8179403227e-15));
    
    // Set global physics object
    vm.setGlobal("physics", physicsObj);
}

// Implementation of old registerPhysicsModule (placeholder)
inline void registerPhysicsModule(Environment& env, HostContext& ctx) {
    (void)env;
    (void)ctx;
}

} // namespace havel::stdlib
