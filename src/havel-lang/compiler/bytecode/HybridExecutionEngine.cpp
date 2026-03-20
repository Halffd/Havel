#include "../../ast/AST.h"
#include "BytecodeIR.hpp"
#include "AstBytecodeCompiler.hpp"
#include "StackVMInterpreter.hpp"
#include <iostream>
#include <unordered_map>

namespace havel::compiler {

// Minimal JIT placeholder used by the hybrid engine.
class SimpleJitCompiler : public JITCompiler {
private:
  std::unordered_map<std::string, bool> compiled_functions;

public:
  SimpleJitCompiler() = default;
  void compileFunction(const BytecodeFunction &func) {
    compiled_functions[func.name] = true;
  }
  BytecodeValue executeCompiled(const std::string &func_name,
                                const std::vector<BytecodeValue> &args) {
    return BytecodeValue{std::in_place_index<0>}; // Placeholder
  }
  bool isCompiled(const std::string &func_name) const {
    return compiled_functions.count(func_name) > 0;
  }
};

// Hybrid engine implementation
HybridExecutionEngine::HybridExecutionEngine(std::unique_ptr<BytecodeCompiler> comp,
                           std::unique_ptr<BytecodeInterpreter> interp,
                           std::unique_ptr<JITCompiler> jcomp)
    : compiler(std::move(comp)), interpreter(std::move(interp)),
      jit(std::move(jcomp)) {}

bool HybridExecutionEngine::compile(const ast::Program &program) {
  try {
    this->current_chunk = this->compiler->compile(program);
    return true;
  } catch (const std::exception &e) {
    std::cerr << "Compilation error: " << e.what() << std::endl;
    return false;
  }
}

BytecodeValue HybridExecutionEngine::execute(const std::string &function_name,
                                    const std::vector<BytecodeValue> &args) {
  if (!this->current_chunk) {
    throw std::runtime_error("No compiled program available");
  }
  return this->interpreter->execute(*this->current_chunk, function_name, args);
}

// Debug options for hybrid engine (placeholder for future use)
struct HybridDebugOptions {
  bool bytecode = false;
  bool jit = false;
};

// Factory function (placeholder)
std::unique_ptr<HybridExecutionEngine> createHybridExecutionEngine() {
  std::unique_ptr<BytecodeCompiler> compiler;
  compiler.reset(new AstBytecodeCompiler());
  auto interpreter = std::make_unique<StackVMInterpreter>();
  auto jit = std::make_unique<SimpleJitCompiler>();
  return std::make_unique<HybridExecutionEngine>(std::move(compiler),
                                        std::move(interpreter), std::move(jit));
}

} // namespace havel::compiler
