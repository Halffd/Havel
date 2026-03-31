#include "CodeEmitter.hpp"
#include "AST.h"

namespace havel::compiler {

CodeEmitter::CodeEmitter(BytecodeChunk& chunk) : chunk_(chunk) {}

void CodeEmitter::beginFunction(BytecodeFunction&& function, std::optional<uint32_t> slot) {
  if (currentContext_.function) {
    savedContexts_.push_back(std::move(currentContext_));
  }

  currentContext_.function = std::make_unique<BytecodeFunction>(std::move(function));
  currentContext_.slot = slot;
  currentContext_.nextLocalIndex = 0;
}

uint32_t CodeEmitter::endFunction() {
  if (!currentContext_.function) {
    throw std::runtime_error("endFunction called without beginFunction");
  }

  uint32_t index = static_cast<uint32_t>(compiledFunctions_.size());
  compiledFunctions_.push_back({std::move(currentContext_.function), currentContext_.slot});

  // Restore previous context if exists
  if (!savedContexts_.empty()) {
    currentContext_ = std::move(savedContexts_.back());
    savedContexts_.pop_back();
  } else {
    currentContext_ = FunctionContext{};
  }

  return index;
}

void CodeEmitter::resetFunctionContext() {
  currentContext_ = FunctionContext{};
  savedContexts_.clear();
}

void CodeEmitter::emit(OpCode op) {
  Instruction instruction;
  instruction.opcode = op;
  if (currentSourceLocation_) {
    instruction.location = currentSourceLocation_;
  }
  addInstruction(instruction);
}

void CodeEmitter::emit(OpCode op, BytecodeValue operand) {
  Instruction instruction;
  instruction.opcode = op;
  instruction.operands.push_back(operand);
  if (currentSourceLocation_) {
    instruction.location = currentSourceLocation_;
  }
  addInstruction(instruction);
}

void CodeEmitter::emit(OpCode op, std::vector<BytecodeValue> operands) {
  Instruction instruction;
  instruction.opcode = op;
  instruction.operands = std::move(operands);
  if (currentSourceLocation_) {
    instruction.location = currentSourceLocation_;
  }
  addInstruction(instruction);
}

uint32_t CodeEmitter::emitJump(OpCode op) {
  emit(op, static_cast<uint32_t>(0)); // Placeholder operand
  return static_cast<uint32_t>(currentContext_.function->instructions.size() - 1);
}

void CodeEmitter::patchJump(uint32_t jumpInstructionIndex, uint32_t target) {
  if (jumpInstructionIndex >= currentContext_.function->instructions.size()) {
    throw std::runtime_error("Invalid jump instruction index");
  }

  auto& instruction = currentContext_.function->instructions[jumpInstructionIndex];
  if (instruction.operands.empty()) {
    instruction.operands.push_back(target);
  } else {
    instruction.operands[0] = target;
  }
}

uint32_t CodeEmitter::addConstant(const BytecodeValue& value) {
  uint32_t index = static_cast<uint32_t>(chunk_.constants.size());
  chunk_.constants.push_back(value);
  return index;
}

const BytecodeValue& CodeEmitter::getConstant(uint32_t index) const {
  if (index >= chunk_.constants.size()) {
    throw std::out_of_range("Constant index out of range");
  }
  return chunk_.constants[index];
}

uint32_t CodeEmitter::reserveLocalSlot() {
  return currentContext_.nextLocalIndex++;
}

uint32_t CodeEmitter::reserveLocalSlot(uint32_t specificSlot) {
  if (specificSlot >= currentContext_.nextLocalIndex) {
    currentContext_.nextLocalIndex = specificSlot + 1;
  }
  return specificSlot;
}

void CodeEmitter::releaseLocalSlot(uint32_t slot) {
  (void)slot; // Slot release for future optimization
}

void CodeEmitter::enterTailPosition() {
  inTailPosition_ = true;
}

void CodeEmitter::exitTailPosition() {
  inTailPosition_ = false;
}

void CodeEmitter::setSourceLocation(uint32_t line, uint32_t column) {
  currentSourceLocation_ = SourceLocation{line, column};
}

void CodeEmitter::clearSourceLocation() {
  currentSourceLocation_.reset();
}

void CodeEmitter::registerFunctionNode(const ast::FunctionDeclaration* node, uint32_t index) {
  functionIndicesByNode_[node] = index;
}

void CodeEmitter::registerLambdaNode(const ast::LambdaExpression* node, uint32_t index) {
  lambdaIndicesByNode_[node] = index;
}

std::optional<uint32_t> CodeEmitter::getFunctionIndex(const ast::FunctionDeclaration* node) const {
  auto it = functionIndicesByNode_.find(node);
  if (it != functionIndicesByNode_.end()) {
    return it->second;
  }
  return std::nullopt;
}

std::optional<uint32_t> CodeEmitter::getLambdaIndex(const ast::LambdaExpression* node) const {
  auto it = lambdaIndicesByNode_.find(node);
  if (it != lambdaIndicesByNode_.end()) {
    return it->second;
  }
  return std::nullopt;
}

void CodeEmitter::addInstruction(const Instruction& instruction) {
  if (!currentContext_.function) {
    throw std::runtime_error("Cannot emit instruction: no active function");
  }
  currentContext_.function->instructions.push_back(instruction);
}

// ============================================================================
// InstructionBuilder Implementation
// ============================================================================

InstructionBuilder::InstructionBuilder(CodeEmitter& emitter) : emitter_(emitter) {}

InstructionBuilder& InstructionBuilder::op(OpCode opcode) {
  opcode_ = opcode;
  return *this;
}

InstructionBuilder& InstructionBuilder::operand(const BytecodeValue& value) {
  operands_.push_back(value);
  return *this;
}

InstructionBuilder& InstructionBuilder::operands(const std::vector<BytecodeValue>& values) {
  operands_.insert(operands_.end(), values.begin(), values.end());
  return *this;
}

InstructionBuilder& InstructionBuilder::atLocation(uint32_t line, uint32_t column) {
  location_ = SourceLocation{line, column};
  return *this;
}

void InstructionBuilder::emit() {
  if (!opcode_) {
    throw std::runtime_error("Cannot emit instruction: no opcode set");
  }

  if (location_) {
    emitter_.setSourceLocation(location_->line, location_->column);
  }

  if (operands_.empty()) {
    emitter_.emit(*opcode_);
  } else if (operands_.size() == 1) {
    emitter_.emit(*opcode_, operands_[0]);
  } else {
    emitter_.emit(*opcode_, operands_);
  }

  // Clear builder state
  opcode_.reset();
  operands_.clear();
  location_.reset();
}

uint32_t InstructionBuilder::emitWithIndex() {
  emit();
  return static_cast<uint32_t>(emitter_.currentFunction().instructions.size() - 1);
}

} // namespace havel::compiler
