#include "JIT.h"
#include "../../utils/Logger.hpp"

#ifdef HAVEL_ENABLE_LLVM
namespace havel::compiler {

JIT::JIT() {
  // Constructor implementation
}

// JIT Hotkey Manager - ULTIMATE PERFORMANCE! ⚡
void JIT::CompileHotkey(const std::string &combination,
                        const ast::Expression &action) {
  // 1. Generate unique function name
  std::string funcName = "hotkey_" + combination;
  std::replace(funcName.begin(), funcName.end(), '+', '_');
  std::replace(funcName.begin(), funcName.end(), ' ', '_');

  // 2. Compile the action to LLVM IR
  llvm::Function *func = compiler.CompileHotkeyAction(action);
  if (!func) {
            havel::error("Failed to compile hotkey action for: {}", combination);
    return;
  }
  func->setName(funcName);

  // 3. JIT compile to native machine code
  auto compiledFunc = compiler.GetCompiledFunction(funcName);
  if (!compiledFunc) {
        havel::error("Failed to JIT compile function: {}", funcName);
    return;
  }

  // 4. Store compiled function
  compiledHotkeys[combination] = compiledFunc;

    havel::info("Compiled hotkey '{}' to native code", combination);
}

void JIT::ExecuteHotkey(const std::string &combination) {
    auto it = compiledHotkeys.find(combination);
    if (it != compiledHotkeys.end() && it->second) {
        it->second();
    } else {
        havel::error("No compiled hotkey found for: {}", combination);
    }
}

void JIT::CompileScript(const ast::Program &program) {
    havel::info("JIT compiling full script...");
  
  // Compile the entire program AST to LLVM IR
  llvm::Function *mainFunc = compiler.CompileProgram(program);
  if (!mainFunc) {
        havel::error("Failed to compile main program script");
      return;
  }

  // JIT compile to native machine code
  auto compiledMain = compiler.GetCompiledFunction("main");
  if (!compiledMain) {
        havel::error("Failed to JIT compile script main entry point");
      return;
  }

    havel::info("Script compiled successfully");
  
  // Execute it!
  compiledMain();
}

} // namespace havel::compiler
#endif // HAVEL_ENABLE_LLVM
