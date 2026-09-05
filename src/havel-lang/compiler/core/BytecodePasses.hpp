#pragma once

#include "BytecodeIR.hpp"

#include <memory>
#include <set>
#include <string>
#include <vector>

namespace havel::compiler {

// ===== Pass Infrastructure =====

enum class PassType {
  // CFG passes
  SimplifyCFG,        // Merge blocks, remove dead, simplify jumps
  // Dataflow passes
  ConstPropagation,   // Fold known constants, propagate types
  CopyPropagation,    // Replace copies of locals
  TypePropagation,    // Infer per-local TYPE_HINT_* masks via dataflow
  DeadCodeElimination, // Remove unreachable/dead code
  // Optimization passes
  Inlining,           // Inline hot call sites
  LICM,               // Loop-invariant code motion
  // Analysis passes
  TypeInference,      // Infer types for locals
  LivenessAnalysis,   // Compute liveness for register allocation
  // Validation pass
  Validation,         // CFG validation
};

// Pass result with modification tracking
struct PassResult {
  bool modified = false;
  bool valid = true;
  std::vector<std::string> messages;
  
  PassResult& operator|=(const PassResult& other) {
    modified |= other.modified;
    valid &= other.valid;
    messages.insert(messages.end(), other.messages.begin(), other.messages.end());
    return *this;
  }
};

// Analysis names used by BytecodePass analysis contracts. Central names avoid
// typo-driven desyncs between pass declarations and the manager's tracking.
namespace Analysis {
inline constexpr const char* kCFG = "cfg";
inline constexpr const char* kLocals = "locals";
inline constexpr const char* kConstantState = "constant_state";
inline constexpr const char* kLiveness = "liveness";
inline constexpr const char* kTypeState = "type_state";
inline constexpr const char* kCopyState = "copy_state";
}  // namespace Analysis

// Base pass interface
class BytecodePass {
public:
  virtual ~BytecodePass() = default;
  virtual PassType type() const = 0;
  virtual std::string name() const = 0;
  virtual PassResult run(std::vector<BasicBlock>& blocks, BytecodeFunction& func) = 0;
  virtual bool requires_validation() const { return true; }
  virtual std::vector<PassType> dependencies() const { return {}; }

  // Analysis contract (TODO.md #9). Each pass declares which analyses it
  // requires (must be valid before it runs), which it preserves (still valid
  // after it runs), and which it invalidates/modifies. The manager enforces
  // the required side by skipping a pass whose requirements are unavailable.
  virtual std::vector<std::string> required_analyses() const { return {}; }
  virtual std::vector<std::string> preserved_analyses() const { return {}; }
  virtual std::vector<std::string> modified_state() const { return {}; }
};

// Pass manager - runs passes in order with validation
class PassManager {
  std::vector<std::unique_ptr<BytecodePass>> passes;
  bool validate_after_each = true;
  // Analyses known to still be valid. Starts with the CFG (passes operate on
  // CFG form); the manager removes entries a pass declares it modified.
  std::set<std::string> available_analyses = {Analysis::kCFG};

public:
  PassManager() = default;

  void add_pass(std::unique_ptr<BytecodePass> pass) {
    passes.push_back(std::move(pass));
  }

  void set_validation(bool enabled) { validate_after_each = enabled; }

  PassResult run_all(std::vector<BasicBlock>& blocks, BytecodeFunction& func) {
    PassResult total;
    for (auto& pass : passes) {
      // Validate dependencies
      for (PassType dep : pass->dependencies()) {
        // In a full implementation, track which passes have run
      }

      // Skip if a required analysis was invalidated by an earlier pass.
      std::vector<std::string> missing;
      for (const std::string& req : pass->required_analyses()) {
        if (!available_analyses.count(req)) missing.push_back(req);
      }
      if (!missing.empty()) {
        PassResult skipped;
        skipped.messages.push_back("Skipping " + pass->name() +
                                   ": missing analysis " + join_errors(missing));
        total |= skipped;
        continue;
      }

      auto result = pass->run(blocks, func);
      total |= result;

      // Analyses a pass invalidates are no longer available; preserved ones
      // stay valid (removing nothing).
      for (const std::string& m : pass->modified_state()) {
        available_analyses.erase(m);
      }

      // Debug/development hardening: after any pass that requests validation,
      // run the full per-function verifier (structural CFG checks plus operand
      // references against the function's locals/upvalues/global_names). Failures
      // are surfaced as messages and force a re-run. The checks are a cheap
      // linear walk, so they stay enabled in all build types.
      if (validate_after_each && pass->requires_validation()) {
        auto validation = validate_function(func, func.entry_block);
        if (!validation.valid) {
          result.messages.push_back("Validation failed after " + pass->name() + ": " + 
                                    join_errors(validation.errors));
          result.modified = true; // Force re-run
        }
      }
    }
    return total;
  }
  
  // Run single pass
  PassResult run_pass(BytecodePass& pass, std::vector<BasicBlock>& blocks, BytecodeFunction& func) {
    return pass.run(blocks, func);
  }
  
private:
  std::string join_errors(const std::vector<std::string>& errors) const {
    std::string result;
    for (size_t i = 0; i < errors.size(); ++i) {
      if (i > 0) result += "; ";
      result += errors[i];
    }
    return result;
  }
};

// ===== Pipeline Creation =====

std::unique_ptr<PassManager> create_standard_pipeline();
std::unique_ptr<PassManager> create_fast_pipeline();
std::unique_ptr<PassManager> create_optimizing_pipeline();

// ===== Pass Declarations =====

class CopyPropagationPass : public BytecodePass {
public:
  PassType type() const override { return PassType::CopyPropagation; }
  std::string name() const override { return "CopyPropagation"; }
  std::vector<PassType> dependencies() const override { return {PassType::ConstPropagation}; }
  std::vector<std::string> required_analyses() const override { return {}; }
  std::vector<std::string> preserved_analyses() const override {
    return {Analysis::kCFG, Analysis::kLocals};
  }
  std::vector<std::string> modified_state() const override { return {Analysis::kCopyState}; }

  PassResult run(std::vector<BasicBlock>& blocks, BytecodeFunction& func) override;
};

class TypePropagationPass : public BytecodePass {
public:
  PassType type() const override { return PassType::TypePropagation; }
  std::string name() const override { return "TypePropagation"; }
  std::vector<PassType> dependencies() const override { return {PassType::ConstPropagation}; }
  std::vector<std::string> required_analyses() const override { return {}; }
  std::vector<std::string> preserved_analyses() const override { return {Analysis::kCFG}; }
  std::vector<std::string> modified_state() const override { return {Analysis::kLocals}; }

  PassResult run(std::vector<BasicBlock>& blocks, BytecodeFunction& func) override;
};

} // namespace havel::compiler