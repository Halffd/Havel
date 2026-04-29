#include "havel-lang/errors/ErrorSystem.h"
#include "VMExecutionContext.hpp"
#include "havel-lang/compiler/core/BytecodeIR.hpp"
#include "VM.hpp"
#include <mutex>
#include <stdexcept>
#include <cmath>

// Macro for throwing errors with source location info
#define COMPILER_THROW(msg) \
  do { \
    ::havel::errors::ErrorReporter::instance().report( \
        HAVEL_ERROR(::havel::errors::ErrorStage::Compiler, msg)); \
    throw std::runtime_error(std::string(msg) + " [" __FILE__ ":" + std::to_string(__LINE__) + "]"); \
  } while (0)

namespace havel::compiler {

// ============================================================================
// CallFrame Implementation
// ============================================================================

const Instruction& CallFrame::currentInstruction() const {
  if (!function || ip >= function->instructions.size()) {
    throw std::out_of_range("Instruction pointer out of bounds");
  }
  return function->instructions[ip];
}

const Instruction& CallFrame::nextInstruction() {
  if (!function || ip >= function->instructions.size()) {
    throw std::out_of_range("No more instructions");
  }
  return function->instructions[ip++];
}

bool CallFrame::hasNext() const {
  return function && ip < function->instructions.size();
}

void CallFrame::jump(uint32_t target) {
  ip = target;
}

// ============================================================================
// CallFrameManager Implementation
// ============================================================================

CallFrameManager::CallFrameManager(size_t maxDepth) : maxDepth_(maxDepth) {
  frames_.reserve(maxDepth);
}

CallFrame& CallFrameManager::pushFrame(const BytecodeFunction* function,
                                        size_t localsBase,
                                        uint32_t closureId) {
  if (frameCount_ >= maxDepth_) {
    COMPILER_THROW("Call stack overflow");
  }

  if (frameCount_ >= frames_.size()) {
    frames_.resize(frameCount_ + 1);
  }

  auto& frame = frames_[frameCount_];
  frame.function = function;
  frame.ip = 0;
  frame.localsBase = localsBase;
  frame.closureId = closureId;
  frame.tryHandlers.clear();

  ++frameCount_;
  return frame;
}

void CallFrameManager::popFrame() {
  if (frameCount_ == 0) {
    COMPILER_THROW("No frame to pop");
  }
  --frameCount_;
}

CallFrame& CallFrameManager::currentFrame() {
  if (frameCount_ == 0) {
    COMPILER_THROW("No active frame");
  }
  return frames_[frameCount_ - 1];
}

const CallFrame& CallFrameManager::currentFrame() const {
  if (frameCount_ == 0) {
    COMPILER_THROW("No active frame");
  }
  return frames_[frameCount_ - 1];
}

CallFrame& CallFrameManager::frameAt(size_t index) {
  if (index >= frameCount_) {
    throw std::out_of_range("Frame index out of range");
  }
  return frames_[index];
}

const CallFrame& CallFrameManager::frameAt(size_t index) const {
  if (index >= frameCount_) {
    throw std::out_of_range("Frame index out of range");
  }
  return frames_[index];
}

std::string CallFrameManager::buildStackTrace() const {
  std::string trace;
  for (size_t i = frameCount_; i > 0; --i) {
    const auto& frame = frames_[i - 1];
    if (frame.function) {
      trace += "  at " + frame.function->name;
      if (frame.ip < frame.function->instructions.size()) {
        const auto& instr = frame.function->instructions[frame.ip];
        if (instr.location) {
          trace += " (" + std::to_string(instr.location->line) + ":" +
                   std::to_string(instr.location->column) + ")";
        }
      }
      trace += "\n";
    }
  }
  return trace;
}

void CallFrameManager::clear() {
  frameCount_ = 0;
  frames_.clear();
}

// ============================================================================
// VMStack Implementation
// ============================================================================

VMStack::VMStack(size_t initialCapacity) {
  // Reserve space if needed
  (void)initialCapacity;
}

void VMStack::push(const Value& value) {
  stack_.push(value);
}

Value VMStack::pop() {
  if (stack_.empty()) {
    COMPILER_THROW("Stack underflow");
  }
  Value value = stack_.top();
  stack_.pop();
  return value;
}

Value& VMStack::peek(size_t distance) {
  // This is tricky with std::stack - we need to access by index
  // For now, throw if trying to peek beyond top
  (void)distance;
  return const_cast<Value&>(stack_.top());
}

const Value& VMStack::peek(size_t distance) const {
  (void)distance;
  return stack_.top();
}

void VMStack::pushMultiple(const std::vector<Value>& values) {
  for (const auto& value : values) {
    push(value);
  }
}

std::vector<Value> VMStack::popMultiple(size_t count) {
  std::vector<Value> result;
  result.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    result.push_back(pop());
  }
  std::reverse(result.begin(), result.end());
  return result;
}

std::vector<Value> VMStack::getValues() const {
  // Copy stack contents
  std::vector<Value> result;
  auto temp = stack_;
  while (!temp.empty()) {
    result.push_back(temp.top());
    temp.pop();
  }
  std::reverse(result.begin(), result.end());
  return result;
}

// ============================================================================
// VMLocals Implementation
// ============================================================================

VMLocals::VMLocals(size_t initialCapacity) {
  ensureCapacity(initialCapacity);
}

Value& VMLocals::get(size_t index) {
  if (index >= locals_.size()) {
    throw std::out_of_range("Local index out of range");
  }
  return locals_[index];
}

const Value& VMLocals::get(size_t index) const {
  if (index >= locals_.size()) {
    throw std::out_of_range("Local index out of range");
  }
  return locals_[index];
}

void VMLocals::set(size_t index, const Value& value) {
  ensureCapacity(index + 1);
  locals_[index] = value;
}

void VMLocals::ensureCapacity(size_t capacity) {
  if (locals_.size() < capacity) {
    locals_.resize(capacity);
  }
}

void VMLocals::resize(size_t newSize) {
  locals_.resize(newSize);
}

size_t VMLocals::reserveSlots(size_t count) {
  size_t base = locals_.size();
  locals_.resize(base + count);
  return base;
}

void VMLocals::releaseSlots(size_t count) {
  if (count > locals_.size()) {
    COMPILER_THROW("Cannot release more slots than available");
  }
  locals_.resize(locals_.size() - count);
}

std::vector<Value> VMLocals::getValues() const {
  return locals_;
}

// ============================================================================
// VMGlobals Implementation
// ============================================================================

void VMGlobals::set(const std::string& name, const Value& value) {
  std::unique_lock lock(mutex_);
  globals_[name] = value;
}

std::optional<Value> VMGlobals::get(const std::string& name) const {
  std::shared_lock lock(mutex_);
  auto it = globals_.find(name);
  if (it != globals_.end()) {
    return it->second;
  }
  return std::nullopt;
}

bool VMGlobals::has(const std::string& name) const {
  std::shared_lock lock(mutex_);
  return globals_.count(name) > 0;
}

void VMGlobals::remove(const std::string& name) {
  std::unique_lock lock(mutex_);
  globals_.erase(name);
}

void VMGlobals::clear() {
  std::unique_lock lock(mutex_);
  globals_.clear();
}

std::unordered_map<std::string, Value> VMGlobals::snapshot() const {
  std::shared_lock lock(mutex_);
  return globals_;
}

void VMGlobals::restore(const std::unordered_map<std::string, Value>& snapshot) {
  std::unique_lock lock(mutex_);
  globals_ = snapshot;
}

// ============================================================================
// VMHostBridge Implementation
// ============================================================================

void VMHostBridge::registerFunction(const std::string& name, HostFunction func) {
  functions_[name] = std::move(func);
}

bool VMHostBridge::hasFunction(const std::string& name) const {
  return functions_.count(name) > 0;
}

Value VMHostBridge::call(const std::string& name,
                                  const std::vector<Value>& args) {
  auto it = functions_.find(name);
  if (it == functions_.end()) {
    COMPILER_THROW("Unknown host function: " + name);
  }
  return it->second(args);
}

std::vector<std::string> VMHostBridge::getRegisteredFunctions() const {
  std::vector<std::string> result;
  for (const auto& [name, _] : functions_) {
    result.push_back(name);
  }
  return result;
}

// ============================================================================
// VMExecutionContext Implementation
// ============================================================================

VMExecutionContext::VMExecutionContext(VM& parent, const BytecodeChunk& chunk)
    : parent_(parent),
      chunk_(chunk),
      upvalues_(parent.getHeap()) {}

Value VMExecutionContext::execute(const std::string& functionName,
                                            const std::vector<Value>& args) {
  // Find function in chunk
  for (size_t i = 0; i < chunk_.getFunctionCount(); ++i) {
    auto func = chunk_.getFunction(static_cast<uint32_t>(i));
    if (func && func->name == functionName) {
      return callFunction(static_cast<uint32_t>(i), args);
    }
  }
  throw std::runtime_error("Function not found: " + functionName);
}

Value VMExecutionContext::callFunction(uint32_t functionIndex,
                                              const std::vector<Value>& args) {
  auto function = chunk_.getFunction(functionIndex);
  if (!function) {
    throw std::out_of_range("Function index out of range");
  }

  // Check arity
  if (args.size() != function->param_count) {
    COMPILER_THROW("Argument count mismatch");
  }

  // Push arguments as locals
  size_t localsBase = locals_.reserveSlots(args.size());
  for (size_t i = 0; i < args.size(); ++i) {
    locals_.set(localsBase + i, args[i]);
  }

  // Push frame
  auto& frame = frames_.pushFrame(function, localsBase);

  // Execute
  isExecuting_ = true;
  clearError();

  try {
    while (frame.hasNext() && isExecuting_) {
      const auto& instruction = frame.nextInstruction();
      executeInstruction(instruction);
    }
  } catch (const std::exception& e) {
    setError(e.what());
  }

  isExecuting_ = false;

  // Pop frame
  frames_.popFrame();

  // Release local slots
  locals_.releaseSlots(args.size());

  // Return value should be on stack
  if (!errorMessage_.empty()) {
    throw std::runtime_error(errorMessage_);
  }

  return stack_.isEmpty() ? nullptr : stack_.pop();
}

Value VMExecutionContext::callClosure(uint32_t closureId,
                                               const std::vector<Value>& args) {
  (void)closureId;
  (void)args;
    // TODO: Implement closure calling
    COMPILER_THROW("Closure calling not yet implemented");
}

void VMExecutionContext::pushValue(const Value& value) {
  stack_.push(value);
}

Value VMExecutionContext::popValue() {
  return stack_.pop();
}

void VMExecutionContext::enterFunction(uint32_t functionIndex,
                                        const std::vector<Value>& args) {
  (void)functionIndex;
  (void)args;
  // TODO: Implement function entry
}

void VMExecutionContext::exitFunction() {
  // TODO: Implement function exit
}

void VMExecutionContext::captureUpvalue(uint32_t localIndex) {
  if (localIndex >= locals_.size()) {
    throw std::out_of_range("Local index out of range");
  }
  upvalues_.openUpvalue(localIndex, locals_.get(localIndex));
}

void VMExecutionContext::closeUpvalues(uint32_t localsBase) {
  // Close upvalues for all locals above base
  for (uint32_t i = localsBase; i < locals_.size(); ++i) {
    if (upvalues_.isUpvalueOpen(i)) {
      auto cell = upvalues_.getOpenUpvalue(i);
      if (cell) {
        cell->close(locals_.get(i));
      }
    }
  }
}

void VMExecutionContext::executeInstruction(const Instruction& instruction) {
  switch (instruction.opcode) {
    case OpCode::LOAD_CONST:
      stack_.push(instruction.operands[0]);
      break;
    case OpCode::LOAD_VAR: {
      uint32_t slot = static_cast<uint32_t>(instruction.operands[0].asInt());
      stack_.push(locals_.get(frames_.currentFrame().localsBase + slot));
      break;
    }
    case OpCode::STORE_VAR: {
      uint32_t slot = static_cast<uint32_t>(instruction.operands[0].asInt());
      locals_.set(frames_.currentFrame().localsBase + slot, stack_.pop());
      break;
    }
    case OpCode::POP:
      stack_.pop();
      break;
case OpCode::ADD:
    case OpCode::SUB:
    case OpCode::MUL:
    case OpCode::DIV:
    case OpCode::INT_DIV:
    case OpCode::DIVMOD:
    case OpCode::REMAINDER:
    case OpCode::MOD:
    case OpCode::POW:
      executeBinaryOp(instruction.opcode);
      break;
    case OpCode::CALL:
      executeCall(static_cast<uint32_t>(instruction.operands[0].asInt()));
      break;
    case OpCode::RETURN:
      executeReturn();
      break;
    default:
      COMPILER_THROW("Unknown opcode");
  }
}

void VMExecutionContext::executeBinaryOp(OpCode op) {
  auto b = stack_.pop();
  auto a = stack_.pop();

  // Simple numeric operations
  if (a.isInt() && b.isInt()) {
    int64_t ai = a.asInt();
    int64_t bi = b.asInt();
    int64_t result = 0;

    switch (op) {
    case OpCode::ADD: result = ai + bi; break;
    case OpCode::SUB: result = ai - bi; break;
    case OpCode::MUL: result = ai * bi; break;
    case OpCode::DIV: {
        if (bi == 0) COMPILER_THROW("Division by zero");
        double dl = static_cast<double>(ai);
        double dr = static_cast<double>(bi);
        stack_.push(Value::makeDouble(dl / dr));
        return;
    }
    case OpCode::INT_DIV: result = ai / bi; break;
    case OpCode::REMAINDER: result = ai % bi; break;
    case OpCode::DIVMOD: {
      if (bi == 0) COMPILER_THROW("Division by zero");
      int64_t rem = ai % bi;
      if (rem != 0 && ((rem < 0) != (bi < 0))) rem += bi;
      int64_t quot = (ai - rem) / bi;
      stack_.push(Value::makeInt(quot));
      stack_.push(Value::makeInt(rem));
      return;
    }
    case OpCode::MOD: {
      if (bi == 0) COMPILER_THROW("Modulo by zero");
      int64_t m = ai % bi;
      if (m != 0 && ((m < 0) != (bi < 0))) m += bi;
      stack_.push(Value::makeInt(m));
      return;
    }
      default: COMPILER_THROW("Unknown binary op");
    }

    stack_.push(Value::makeInt(result));
  } else if (a.isDouble() && b.isDouble()) {
    double ad = a.asDouble();
    double bd = b.asDouble();
    double result = 0;

    switch (op) {
    case OpCode::ADD: result = ad + bd; break;
    case OpCode::SUB: result = ad - bd; break;
    case OpCode::MUL: result = ad * bd; break;
    case OpCode::DIV: result = ad / bd; break;
    case OpCode::INT_DIV: result = static_cast<double>(static_cast<int64_t>(ad) / static_cast<int64_t>(bd)); break;
    case OpCode::REMAINDER: result = static_cast<double>(static_cast<int64_t>(ad) % static_cast<int64_t>(bd)); break;
    case OpCode::MOD: {
      double m = std::fmod(ad, bd);
      if (m != 0.0 && ((m < 0.0) != (bd < 0.0))) m += bd;
      stack_.push(Value::makeDouble(m));
      return;
    }
      default: COMPILER_THROW("Unknown binary op");
    }

    stack_.push(Value::makeDouble(result));
  } else {
    COMPILER_THROW("Type error in binary operation");
  }
}

void VMExecutionContext::executeCall(uint32_t argCount) {
  (void)argCount;
    // TODO: Implement function calling
    COMPILER_THROW("CALL not fully implemented");
}

void VMExecutionContext::executeReturn() {
  isExecuting_ = false;
}

std::vector<Value> VMExecutionContext::getGCRoots() const {
  std::vector<Value> roots;

  // Stack roots
  auto stackRoots = stack_.getValues();
  roots.insert(roots.end(), stackRoots.begin(), stackRoots.end());

  // Local roots
  auto localRoots = locals_.getValues();
  roots.insert(roots.end(), localRoots.begin(), localRoots.end());

  return roots;
}

void VMExecutionContext::setError(const std::string& message) {
  errorMessage_ = message;
}

void VMExecutionContext::clearError() {
  errorMessage_.clear();
}

} // namespace havel::compiler
