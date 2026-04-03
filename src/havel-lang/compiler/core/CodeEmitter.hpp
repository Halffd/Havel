#pragma once

#include "BytecodeIR.hpp"
#include "CompilerUtils.hpp"
#include <vector>
#include <memory>
#include <optional>

// Qt defines 'emit' as a macro - we need to undefine it for our method name
#ifdef emit
#undef emit
#endif

namespace havel::compiler {

// ============================================================================
// CodeEmitter - Handles bytecode instruction emission and function construction
// ============================================================================
class CodeEmitter {
public:
  struct FunctionContext {
    std::unique_ptr<BytecodeFunction> function;
    std::optional<uint32_t> slot;
    uint32_t nextLocalIndex = 0;
  };

  explicit CodeEmitter(BytecodeChunk& chunk);

  // ============================================================================
  // Function management
  // ============================================================================
  void beginFunction(BytecodeFunction&& function, std::optional<uint32_t> slot = std::nullopt);
  uint32_t endFunction();
  void resetFunctionContext();

  // ============================================================================
  // Instruction emission
  // ============================================================================
  void emit(OpCode op);
  void emit(OpCode op, BytecodeValue operand);
  void emit(OpCode op, std::vector<BytecodeValue> operands);

  // ============================================================================
  // Jump handling
  // ============================================================================
  uint32_t emitJump(OpCode op);
  void patchJump(uint32_t jumpInstructionIndex, uint32_t target);

  // ============================================================================
  // Constant management
  // ============================================================================
  uint32_t addConstant(const BytecodeValue& value);
  uint32_t addStringConstant(const std::string& str);
  const BytecodeValue& getConstant(uint32_t index) const;

  // ============================================================================
  // Local slot management
  // ============================================================================
  uint32_t reserveLocalSlot();
  uint32_t reserveLocalSlot(uint32_t specificSlot);
  void releaseLocalSlot(uint32_t slot);
  uint32_t getNextLocalIndex() const { return currentContext_.nextLocalIndex; }

  // ============================================================================
  // Tail call optimization
  // ============================================================================
  void enterTailPosition();
  void exitTailPosition();
  bool isInTailPosition() const { return inTailPosition_; }
  bool wasTailCall() const { return emittedTailCall_; }
  void clearTailCallFlag() { emittedTailCall_ = false; }

  // ============================================================================
  // Source location tracking
  // ============================================================================
  void setSourceLocation(uint32_t line, uint32_t column);
  void clearSourceLocation();

  // ============================================================================
  // State queries
  // ============================================================================
  bool isInFunction() const { return currentContext_.function != nullptr; }
  BytecodeFunction& currentFunction() { return *currentContext_.function; }
  const BytecodeFunction& currentFunction() const { return *currentContext_.function; }
  BytecodeChunk& getChunk() { return chunk_; }

  // ============================================================================
  // Function index management
  // ============================================================================
  void registerFunctionNode(const ast::FunctionDeclaration* node, uint32_t index);
  void registerLambdaNode(const ast::LambdaExpression* node, uint32_t index);
  std::optional<uint32_t> getFunctionIndex(const ast::FunctionDeclaration* node) const;
  std::optional<uint32_t> getLambdaIndex(const ast::LambdaExpression* node) const;

private:
  BytecodeChunk& chunk_;
  FunctionContext currentContext_;
  std::vector<FunctionContext> savedContexts_;
  std::vector<std::pair<std::unique_ptr<BytecodeFunction>, std::optional<uint32_t>>> compiledFunctions_;
  std::unordered_map<const ast::FunctionDeclaration*, uint32_t> functionIndicesByNode_;
  std::unordered_map<const ast::LambdaExpression*, uint32_t> lambdaIndicesByNode_;

  bool inTailPosition_ = false;
  bool emittedTailCall_ = false;
  std::optional<SourceLocation> currentSourceLocation_;

  void addInstruction(const Instruction& instruction);
};

// ============================================================================
// InstructionBuilder - Fluent interface for building instructions
// ============================================================================
class InstructionBuilder {
public:
  explicit InstructionBuilder(CodeEmitter& emitter);

  InstructionBuilder& op(OpCode opcode);
  InstructionBuilder& operand(const BytecodeValue& value);
  InstructionBuilder& operands(const std::vector<BytecodeValue>& values);
  InstructionBuilder& atLocation(uint32_t line, uint32_t column);

  void emit();
  uint32_t emitWithIndex();

private:
  CodeEmitter& emitter_;
  std::optional<OpCode> opcode_;
  std::vector<BytecodeValue> operands_;
  std::optional<SourceLocation> location_;
};

} // namespace havel::compiler
