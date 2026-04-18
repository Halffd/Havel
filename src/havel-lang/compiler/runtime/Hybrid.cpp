#include "havel-lang/errors/ErrorSystem.h"
#include "../../ast/AST.h"
#include "../core/BytecodeIR.hpp"
#include "../core/ByteCompiler.hpp"
#include "../vm/VM.hpp"
#include "../core/CompilerAPI.hpp"

#include <iostream>
#include <unordered_map>
#include <stdexcept>

#ifdef HAVEL_ENABLE_LLVM
#include "../BytecodeOrcJIT.h"
#endif


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
  Value executeCompiled(VM* vm, const std::string &func_name,
                                const std::vector<Value> &args) override {
    (void)vm;
    return Value::makeNull(); // Placeholder
  }

  bool isCompiled(const std::string &func_name) const {
    return compiled_functions.count(func_name) > 0;
  }
};

// Hybrid engine implementation
Hybrid::Hybrid(std::unique_ptr<BytecodeCompiler> comp,
                           std::unique_ptr<BytecodeInterpreter> interp,
                           std::unique_ptr<JITCompiler> jcomp,
                           bool use_jit)
    : compiler(std::move(comp)), interpreter(std::move(interp)),
      jit(std::move(jcomp)) {
  this->jit_enabled = use_jit;
  
  // Connect JIT trigger if enabled
  auto* vm = dynamic_cast<VM*>(this->interpreter.get());
  if (vm && this->jit_enabled) {
    vm->setHotFunctionCallback([this](const BytecodeFunction& func) {
      if (this->jit_enabled && !this->jit->isCompiled(func.name)) {
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

  // Phase 4 JIT: Divert to native execution if compiled AND JIT is enabled
  if (this->jit_enabled && this->jit->isCompiled(function_name)) {
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

// Factory function

std::unique_ptr<Hybrid> createHybrid(const CompileOptions& options) {
  std::unique_ptr<BytecodeCompiler> compiler;
  compiler.reset(new ByteCompiler());
  auto interpreter = std::make_unique<VM>();
  
#ifdef HAVEL_ENABLE_LLVM
  auto jit = std::make_unique<BytecodeOrcJIT>();
  auto* orcJit = static_cast<BytecodeOrcJIT*>(jit.get());
  if (options.debugJIT) {
    orcJit->setDebugMode(true);
  }
  if (options.dumpIR) {
    orcJit->setDumpIR(true);
  }
  if (options.outputAsm) {
    orcJit->setDumpAsmToFile(true);
  }
#else
  auto jit = std::make_unique<SimpleJitCompiler>();
#endif


  return std::make_unique<Hybrid>(std::move(compiler),
                                        std::move(interpreter), std::move(jit),
                                        options.useJIT);
}


} // namespace havel::compiler
