#include "JIT.h"

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
    std::cerr << "Failed to compile hotkey action for: " << combination
              << std::endl;
    return;
  }
  func->setName(funcName);

  // 3. JIT compile to native machine code
  auto compiledFunc = compiler.GetCompiledFunction(funcName);
  if (!compiledFunc) {
    std::cerr << "Failed to JIT compile function: " << funcName << std::endl;
    return;
  }

  // 4. Store compiled function
  compiledHotkeys[combination] = compiledFunc;

  std::cout << "🔥 Compiled hotkey '" << combination << "' to native code!"
            << std::endl;
}

void JIT::ExecuteHotkey(const std::string &combination) {
  auto it = compiledHotkeys.find(combination);
  if (it != compiledHotkeys.end() && it->second) {
    // Execute pure machine code - BLAZING FAST! ⚡
    it->second();
  } else {
    std::cerr << "No compiled hotkey found for: " << combination << std::endl;
  }
}

void JIT::CompileScript(const ast::Program &program) {
  std::cout << "🚀 JIT compiling full script..." << std::endl;
  
  // Compile the entire program AST to LLVM IR
  llvm::Function *mainFunc = compiler.CompileProgram(program);
  if (!mainFunc) {
      std::cerr << "Failed to compile main program script" << std::endl;
      return;
  }

  // JIT compile to native machine code
  auto compiledMain = compiler.GetCompiledFunction("main");
  if (!compiledMain) {
      std::cerr << "Failed to JIT compile script main entry point" << std::endl;
      return;
  }

  std::cout << "🔥 Script compiled successfully! Ready for blazing fast execution! ⚡" << std::endl;
  
  // Execute it!
  compiledMain();
}

} // namespace havel::compiler
#endif // HAVEL_ENABLE_LLVM
