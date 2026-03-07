/*
 * PhysicsModule.cpp
 * 
 * Physics constants module for Havel language.
 */
#include "../../host/HostContext.hpp"
#include "../runtime/Environment.hpp"

namespace havel::modules {

void registerPhysicsModule(Environment& env, HostContext&) {
    auto physics = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    
    // Speed of light (m/s)
    (*physics)["c"] = HavelValue(299792458.0);
    
    // Gravitational constant (N⋅m²/kg²)
    (*physics)["G"] = HavelValue(6.67430e-11);
    
    // Elementary charge (C)
    (*physics)["e"] = HavelValue(1.602176634e-19);
    
    // Electron mass (kg)
    (*physics)["me"] = HavelValue(9.10938356e-31);
    
    // Proton mass (kg)
    (*physics)["mp"] = HavelValue(1.67262192369e-27);
    
    // Planck constant (J⋅s)
    (*physics)["h"] = HavelValue(6.62607015e-34);
    
    // Avogadro constant (mol⁻¹)
    (*physics)["NA"] = HavelValue(6.02214076e23);
    
    // Boltzmann constant (J/K)
    (*physics)["k"] = HavelValue(1.380649e-23);
    
    // Permittivity of free space (F/m)
    (*physics)["epsilon0"] = HavelValue(8.854187817e-12);
    
    // Permeability of free space (H/m)
    (*physics)["mu0"] = HavelValue(1.25663706212e-6);
    
    // Fine structure constant
    (*physics)["alpha"] = HavelValue(7.2973525693e-3);
    
    // Rydberg constant (J)
    (*physics)["Rinf"] = HavelValue(10973731.56816);
    
    // Stefan-Boltzmann constant (W⋅m⁻²⋅K⁻⁴)
    (*physics)["sigma"] = HavelValue(5.670374419e-8);
    
    // Electron volt (J)
    (*physics)["eV"] = HavelValue(1.602176634e-19);
    
    // Atomic mass unit (kg)
    (*physics)["u"] = HavelValue(1.66053906660e-27);
    
    // Bohr radius (m)
    (*physics)["a0"] = HavelValue(5.29177210903e-11);
    
    // Classical electron radius (m)
    (*physics)["re"] = HavelValue(2.8179403227e-15);
    
    env.Define("physics", HavelValue(physics));
}

} // namespace havel::modules
