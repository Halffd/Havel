/* PhysicsModule.cpp - VM-native stdlib module */
#include "PhysicsModule.hpp"

using havel::compiler::BytecodeValue;
using havel::compiler::VMApi;

namespace havel::stdlib {

// Register physics module with VMApi (stable API layer)
void registerPhysicsModule(VMApi &api) {
  // Physics constants
  api.setGlobal("G",
                BytecodeValue(9.80665)); // Gravitational acceleration (m/s²)
  api.setGlobal("C", BytecodeValue(299792458.0)); // Speed of light (m/s)
  api.setGlobal(
      "G0", BytecodeValue(6.67430e-11)); // Gravitational constant (N⋅m²/kg²)
  api.setGlobal("K", BytecodeValue(1.380649e-23)); // Boltzmann constant (J/K)
  api.setGlobal("NA",
                BytecodeValue(6.02214076e23));    // Avogadro constant (mol⁻¹)
  api.setGlobal("R", BytecodeValue(8.314462618)); // Gas constant (J/(mol⋅K))
  api.setGlobal("ME", BytecodeValue(9.1093837015e-31));  // Electron mass (kg)
  api.setGlobal("MP", BytecodeValue(1.67262192369e-27)); // Proton mass (kg)
  api.setGlobal("E", BytecodeValue(1.602176634e-19)); // Elementary charge (C)
  api.setGlobal("H", BytecodeValue(6.62607015e-34));  // Planck constant (J⋅s)
  api.setGlobal("EPS0",
                BytecodeValue(8.8541878128e-12)); // Vacuum permittivity (F/m)
  api.setGlobal("MU0",
                BytecodeValue(1.25663706212e-6)); // Vacuum permeability (N/A²)

  // Physics functions

  // force(mass, acceleration) - Calculate force (F = ma)
  api.registerFunction("force", [&api](const std::vector<BytecodeValue> &args) {
    if (args.size() < 2)
      throw std::runtime_error("force() requires mass and acceleration");

    double mass = 0.0, accel = 0.0;

    if (std::holds_alternative<int64_t>(args[0]))
      mass = static_cast<double>(std::get<int64_t>(args[0]));
    else if (std::holds_alternative<double>(args[0]))
      mass = std::get<double>(args[0]);
    else
      throw std::runtime_error("force() mass must be a number");

    if (std::holds_alternative<int64_t>(args[1]))
      accel = static_cast<double>(std::get<int64_t>(args[1]));
    else if (std::holds_alternative<double>(args[1]))
      accel = std::get<double>(args[1]);
    else
      throw std::runtime_error("force() acceleration must be a number");

    return BytecodeValue(mass * accel);
  });

  // kinetic_energy(mass, velocity) - Calculate kinetic energy (KE = 0.5mv²)
  api.registerFunction(
      "kinetic_energy", [&api](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2)
          throw std::runtime_error(
              "kinetic_energy() requires mass and velocity");

        double mass = 0.0, velocity = 0.0;

        if (std::holds_alternative<int64_t>(args[0]))
          mass = static_cast<double>(std::get<int64_t>(args[0]));
        else if (std::holds_alternative<double>(args[0]))
          mass = std::get<double>(args[0]);
        else
          throw std::runtime_error("kinetic_energy() mass must be a number");

        if (std::holds_alternative<int64_t>(args[1]))
          velocity = static_cast<double>(std::get<int64_t>(args[1]));
        else if (std::holds_alternative<double>(args[1]))
          velocity = std::get<double>(args[1]);
        else
          throw std::runtime_error(
              "kinetic_energy() velocity must be a number");

        return BytecodeValue(0.5 * mass * velocity * velocity);
      });

  // potential_energy(mass, height, gravity) - Calculate potential energy (PE =
  // mgh)
  api.registerFunction(
      "potential_energy", [&api](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2)
          throw std::runtime_error(
              "potential_energy() requires mass and height");

        double mass = 0.0, height = 0.0, gravity = 9.80665;

        if (std::holds_alternative<int64_t>(args[0]))
          mass = static_cast<double>(std::get<int64_t>(args[0]));
        else if (std::holds_alternative<double>(args[0]))
          mass = std::get<double>(args[0]);
        else
          throw std::runtime_error("potential_energy() mass must be a number");

        if (std::holds_alternative<int64_t>(args[1]))
          height = static_cast<double>(std::get<int64_t>(args[1]));
        else if (std::holds_alternative<double>(args[1]))
          height = std::get<double>(args[1]);
        else
          throw std::runtime_error(
              "potential_energy() height must be a number");

        if (args.size() >= 3) {
          if (std::holds_alternative<int64_t>(args[2]))
            gravity = static_cast<double>(std::get<int64_t>(args[2]));
          else if (std::holds_alternative<double>(args[2]))
            gravity = std::get<double>(args[2]);
          else
            throw std::runtime_error(
                "potential_energy() gravity must be a number");
        }

        return BytecodeValue(mass * gravity * height);
      });

  // momentum(mass, velocity) - Calculate momentum (p = mv)
  api.registerFunction(
      "momentum", [&api](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2)
          throw std::runtime_error("momentum() requires mass and velocity");

        double mass = 0.0, velocity = 0.0;

        if (std::holds_alternative<int64_t>(args[0]))
          mass = static_cast<double>(std::get<int64_t>(args[0]));
        else if (std::holds_alternative<double>(args[0]))
          mass = std::get<double>(args[0]);
        else
          throw std::runtime_error("momentum() mass must be a number");

        if (std::holds_alternative<int64_t>(args[1]))
          velocity = static_cast<double>(std::get<int64_t>(args[1]));
        else if (std::holds_alternative<double>(args[1]))
          velocity = std::get<double>(args[1]);
        else
          throw std::runtime_error("momentum() velocity must be a number");

        return BytecodeValue(mass * velocity);
      });

  // wavelength(frequency) - Calculate wavelength (λ = c/f)
  api.registerFunction(
      "wavelength", [&api](const std::vector<BytecodeValue> &args) {
        if (args.empty())
          throw std::runtime_error("wavelength() requires frequency");

        double frequency = 0.0;

        if (std::holds_alternative<int64_t>(args[0]))
          frequency = static_cast<double>(std::get<int64_t>(args[0]));
        else if (std::holds_alternative<double>(args[0]))
          frequency = std::get<double>(args[0]);
        else
          throw std::runtime_error("wavelength() frequency must be a number");

        if (frequency <= 0.0)
          throw std::runtime_error("wavelength() frequency must be positive");

        return BytecodeValue(299792458.0 / frequency); // c / f
      });

  // Register physics object
  auto physicsObj = api.makeObject();
  api.setField(physicsObj, "force", api.makeFunctionRef("force"));
  api.setField(physicsObj, "kinetic_energy",
               api.makeFunctionRef("kinetic_energy"));
  api.setField(physicsObj, "potential_energy",
               api.makeFunctionRef("potential_energy"));
  api.setField(physicsObj, "momentum", api.makeFunctionRef("momentum"));
  api.setField(physicsObj, "wavelength", api.makeFunctionRef("wavelength"));
  api.setGlobal("Physics", physicsObj);
}

} // namespace havel::stdlib
