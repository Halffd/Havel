#include "havel-lang/errors/ErrorSystem.h"
#include "../../ast/AST.h"
#include "../core/BytecodeIR.hpp"
#include "../core/ByteCompiler.hpp"
#include "../vm/VM.hpp"
#include <iostream>
#include <unordered_map>
#include <stdexcept>

// Macro for throwing errors with source location info
#define COMPILER_THROW(msg) \
  do { \
    ::havel::errors::ErrorReporter::instance().report( \
        HAVEL_ERROR(::havel::errors::ErrorStage::Compiler, msg)); \
    throw std::runtime_error(std::string(msg) + " [" __FILE__ ":" + std::to_string(__LINE__) + "]"); \
  } while (0)

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
  Value executeCompiled(const std::string &func_name,
                                const std::vector<Value> &args) {
    return Value::makeNull(); // Placeholder
  }
  bool isCompiled(const std::string &func_name) const {
    return compiled_functions.count(func_name) > 0;
  }
};

// Hybrid engine implementation
Hybrid::Hybrid(std::unique_ptr<BytecodeCompiler> comp,
                           std::unique_ptr<BytecodeInterpreter> interp,
                           std::unique_ptr<JITCompiler> jcomp)
    : compiler(std::move(comp)), interpreter(std::move(interp)),
      jit(std::move(jcomp)) {
  
  // Connect JIT trigger
  auto* vm = dynamic_cast<VM*>(this->interpreter.get());
  if (vm) {
    vm->setHotFunctionCallback([this](BytecodeFunction& func) {
      if (!this->jit->isCompiled(func.name)) {
        this->jit->compileFunction(func);
      }
    });
  }
}

bool Hybrid::compile(const ast::Program &program) {
  try {
    this->current_chunk = this->compiler->compile(program);
    return true;
  } catch (const std::exception &e) {
    std::cerr << "Compilation error: " << e.what() << std::endl;
    return false;
  }
}

Value Hybrid::execute(const std::string &function_name,
                                    const std::vector<Value> &args) {
  if (!this->current_chunk) {
    COMPILER_THROW("No compiled program available");
  }

  // Phase 4 JIT: Divert to native execution if compiled
  if (this->jit->isCompiled(function_name)) {
    auto* vm = dynamic_cast<VM*>(this->interpreter.get());
    return this->jit->executeCompiled(vm, function_name, args);
  }

  return this->interpreter->execute(*this->current_chunk, function_name, args);
}

// Debug options for hybrid engine (placeholder for future use)
struct HybridDebugOptions {
  bool bytecode = false;
  bool jit = false;
};

#ifdef HAVEL_ENABLE_LLVM
#include "../BytecodeOrcJIT.h"
#endif

// Factory function (placeholder)
std::unique_ptr<Hybrid> createHybrid() {
  std::unique_ptr<BytecodeCompiler> compiler;
  compiler.reset(new ByteCompiler());
  auto interpreter = std::make_unique<VM>();
  
#ifdef HAVEL_ENABLE_LLVM
  auto jit = std::make_unique<BytecodeOrcJIT>();
#else
  auto jit = std::make_unique<SimpleJitCompiler>();
#endif

  return std::make_unique<Hybrid>(std::move(compiler),
                                        std::move(interpreter), std::move(jit));
}

} // namespace havel::compiler
