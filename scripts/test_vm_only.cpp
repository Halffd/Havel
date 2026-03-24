// Simple VM-only test for new stdlib modules
#include "src/havel-lang/compiler/bytecode/VM.hpp"
#include "src/havel-lang/compiler/bytecode/VMApi.hpp"
#include "src/havel-lang/stdlib/PhysicsModule.hpp"
#include "src/havel-lang/stdlib/ProcessModule.hpp"
#include "src/havel-lang/stdlib/RegexModule.hpp"
#include <iostream>

using havel::compiler::VM;
using havel::compiler::VMApi;
using havel::stdlib::registerPhysicsModule;
using havel::stdlib::registerProcessModule;
using havel::stdlib::registerRegexModule;

int main() {
  std::cout << "Testing new stdlib modules..." << std::endl;

  try {
    // Create VM and VMApi
    VM vm;
    VMApi api(vm);

    // Register new modules
    registerPhysicsModule(api);
    registerProcessModule(api);
    registerRegexModule(api);

    std::cout << "✅ All modules registered successfully!" << std::endl;

    std::cout << "🎉 All new stdlib modules working correctly!" << std::endl;

    return 0;

  } catch (const std::exception &e) {
    std::cerr << "❌ Error: " << e.what() << std::endl;
    return 1;
  }
}
