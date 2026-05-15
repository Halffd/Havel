// StdLibModules.hpp
// Standard library module registration
// Simple, explicit, no magic

#pragma once
#include "ModuleLoader.hpp"
#include "../compiler/runtime/HostBridge.hpp"

namespace havel {

class Interpreter;

// Register all standard library modules
void registerStdLibModules(ModuleLoader &loader);

// Load all stdlib modules into environment
void loadStdLibModules(Environment &env, ModuleLoader &loader,
                       Interpreter *interpreter = nullptr);

// Register stdlib modules with VM (VM-native, no Environment)
void registerStdLibWithVM(compiler::HostBridge& bridge);

// Register ONLY pure stdlib modules with VM
namespace compiler { class VM; }
void registerPureStdLib(compiler::VM& vm);

} // namespace havel
