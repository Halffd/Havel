#pragma once

// ===== Compiler backend abstraction (TODO.md #18 / #19) =====
//
// A CompilerBackend consumes validated BytecodeIR (BytecodeFunction /
// BytecodeChunk) and produces executable code behind a uniform interface.
// Backends never see Havel AST: source -> AST -> (ByteCompiler) -> IR ->
// (pass pipeline) -> backend.
//
// Known implementations:
//   - VMBbackend: the interpreter. The VM stays the simplest backend and the
//     known-correct reference implementation (TODO #19): "compile" is a no-op
//     marker and execution dispatches through the VM's bytecode interpreter.
//   - LlvmOrcBackend: adapter over the legacy BytecodeOrcJIT (JITCompiler),
//     so the ORC implementation becomes a backend behind this boundary
//     instead of being wired directly into engine code.
//
// The tier contract matches JITCompiler: tier 1 = baseline compile,
// tier 2 = optimizing/background compile.

#include "BytecodeIR.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace havel::compiler {

class CompilerBackend {
public:
  virtual ~CompilerBackend() = default;

  // Compile one validated function. Returns true when the function is
  // executable through this backend afterwards.
  virtual bool compile(const BytecodeFunction& func) = 0;
  // Compile every function of a validated module. Returns false if any
  // function fails; functions that already succeeded stay compiled.
  virtual bool compile_module(const BytecodeChunk& chunk) {
    bool all = true;
    const size_t count = chunk.getFunctionCount();
    for (size_t i = 0; i < count; ++i) {
      const BytecodeFunction* fn = chunk.getFunction(static_cast<uint32_t>(i));
      if (fn && !compile(*fn)) all = false;
    }
    return all;
  }
  // Tier-aware compilation contract (see class comment).
  virtual bool compile_tier(const BytecodeFunction& func, uint8_t tier) {
    (void)tier;
    return compile(func);
  }

  // Whether a previously compiled function is executable via this backend.
  virtual bool is_compiled(const std::string& func_name) const = 0;

  // Diagnostic knobs, mirroring the JITCompiler set.
  virtual void set_debug_mode(bool enabled) { (void)enabled; }
  virtual void set_dump_ir(bool enabled) { (void)enabled; }
  virtual void set_dump_asm(bool enabled) { (void)enabled; }
  virtual void set_show_warnings(bool enabled) { (void)enabled; }
  virtual void set_optimization_level(uint8_t level) { (void)level; }

  // Human-readable backend name for diagnostics.
  virtual const char* name() const = 0;
};

// The interpreter backend: always compiled, executes through the VM.
class VMBbackend final : public CompilerBackend {
public:
  bool compile(const BytecodeFunction& func) override {
    (void)func;  // Nothing to lower: the VM executes the bytecode directly.
    return true;
  }
  bool is_compiled(const std::string& func_name) const override {
    // The interpreter can run anything; a name check is meaningless here,
    // so report availability only for non-empty names.
    return !func_name.empty();
  }
  const char* name() const override { return "vm"; }
};

// Adapter presenting the legacy JITCompiler (BytecodeOrcJIT) as a
// CompilerBackend. The ORC JIT keeps its own interface; new engine code
// talks to it through this boundary only.
//
// Two ownership flavors:
//  - owning: constructed with a unique_ptr<JITCompiler>;
//  - view:   constructed with a raw pointer the caller (e.g. the VM, via
//    setJITCompiler) already owns. The view must not outlive the owner.
class JITCompilerBackend final : public CompilerBackend {
public:
  explicit JITCompilerBackend(std::unique_ptr<JITCompiler> jit)
      : jit_(std::move(jit)), owning_(true) {}
  explicit JITCompilerBackend(JITCompiler* jit_view)
      : jit_(jit_view), owning_(false) {}

  JITCompilerBackend(const JITCompilerBackend&) = delete;
  JITCompilerBackend& operator=(const JITCompilerBackend&) = delete;

  ~JITCompilerBackend() override {
    if (!owning_) {
      // Release the view without deleting: a default-constructed unique_ptr
      // with a non-null stored pointer would delete it.
      jit_.release();
    }
  }

  bool compile(const BytecodeFunction& func) override {
    if (!jit_) return false;
    jit_->compileFunction(func);
    return true;
  }
  bool compile_tier(const BytecodeFunction& func, uint8_t tier) override {
    if (!jit_) return false;
    jit_->compileFunctionTier(func, tier);
    return true;
  }
  bool is_compiled(const std::string& func_name) const override {
    return jit_ && jit_->isCompiled(func_name);
  }

  void set_debug_mode(bool enabled) override { if (jit_) jit_->setDebugMode(enabled); }
  void set_dump_ir(bool enabled) override { if (jit_) jit_->setDumpIR(enabled); }
  void set_dump_asm(bool enabled) override { if (jit_) jit_->setDumpAsmToFile(enabled); }
  void set_show_warnings(bool enabled) override { if (jit_) jit_->setShowWarnings(enabled); }
  void set_optimization_level(uint8_t level) override {
    if (jit_) jit_->setOptimizationLevel(level);
  }

  const char* name() const override { return "llvm-orc"; }

  // Access for embedders that still drive the legacy interface directly
  // (VM tiering hooks, trace compilation).
  JITCompiler* legacy() const { return jit_.get(); }

private:
  std::unique_ptr<JITCompiler> jit_;
  bool owning_;
};

}  // namespace havel::compiler
