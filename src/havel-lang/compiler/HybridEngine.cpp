#include "../ast/AST.h"
#include "../runtime/Interpreter.hpp"
#include "Bytecode.h"
#include "BytecodeCompiler.hpp"
#include <iostream>
#include <unordered_map>

namespace havel::compiler {

// Simple bytecode interpreter (placeholder implementation)
class HavelBytecodeInterpreter : public BytecodeInterpreter {
private:
  bool debug_mode = false;

public:
  HavelBytecodeInterpreter() = default;
  BytecodeValue execute(const BytecodeChunk &chunk,
                        const std::string &function_name,
                        const std::vector<BytecodeValue> &args = {}) override {
    // Placeholder: return null for now
    return BytecodeValue{std::in_place_index<0>};
  }
  void setDebugMode(bool enabled) override { debug_mode = enabled; }
};

// Simple JIT compiler (placeholder implementation)
class HavelJITCompiler : public JITCompiler {
private:
  std::unordered_map<std::string, bool> compiled_functions;

public:
  HavelJITCompiler() = default;
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
HybridEngine::HybridEngine(std::unique_ptr<BytecodeCompiler> comp,
                           std::unique_ptr<BytecodeInterpreter> interp,
                           std::unique_ptr<JITCompiler> jcomp)
    : compiler(std::move(comp)), interpreter(std::move(interp)),
      jit(std::move(jcomp)) {}

bool HybridEngine::compile(const ast::Program &program) {
  try {
    this->current_chunk = this->compiler->compile(program);
    return true;
  } catch (const std::exception &e) {
    std::cerr << "Compilation error: " << e.what() << std::endl;
    return false;
  }
}

BytecodeValue HybridEngine::execute(const std::string &function_name,
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
std::unique_ptr<HybridEngine> createHybridEngine() {
  std::unique_ptr<BytecodeCompiler> compiler;
  compiler.reset(new HavelBytecodeCompiler());
  auto interpreter = std::make_unique<HavelBytecodeInterpreter>();
  auto jit = std::make_unique<HavelJITCompiler>();
  return std::make_unique<HybridEngine>(std::move(compiler),
                                        std::move(interpreter), std::move(jit));
}

} // namespace havel::compiler
