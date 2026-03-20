#include "VM.hpp"

#include <chrono>
#include <cmath>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace havel::compiler {

namespace {
std::string toString(const BytecodeValue &value) {
  if (std::holds_alternative<std::nullptr_t>(value)) {
    return "null";
  }
  if (std::holds_alternative<bool>(value)) {
    return std::get<bool>(value) ? "true" : "false";
  }
  if (std::holds_alternative<int64_t>(value)) {
    return std::to_string(std::get<int64_t>(value));
  }
  if (std::holds_alternative<double>(value)) {
    std::ostringstream out;
    out << std::get<double>(value);
    return out.str();
  }
  if (std::holds_alternative<std::string>(value)) {
    return std::get<std::string>(value);
  }
  if (std::holds_alternative<uint32_t>(value)) {
    return "const[" + std::to_string(std::get<uint32_t>(value)) + "]";
  }
  if (std::holds_alternative<FunctionObject>(value)) {
    return "fn[" +
           std::to_string(std::get<FunctionObject>(value).function_index) + "]";
  }
  if (std::holds_alternative<ClosureRef>(value)) {
    return "closure[" + std::to_string(std::get<ClosureRef>(value).id) + "]";
  }
  if (std::holds_alternative<ArrayRef>(value)) {
    return "array[" + std::to_string(std::get<ArrayRef>(value).id) + "]";
  }
  if (std::holds_alternative<ObjectRef>(value)) {
    return "object[" + std::to_string(std::get<ObjectRef>(value).id) + "]";
  }
  if (std::holds_alternative<SetRef>(value)) {
    return "set[" + std::to_string(std::get<SetRef>(value).id) + "]";
  }
  return "unknown";
}

std::optional<int64_t> indexFromValue(const BytecodeValue &value) {
  if (std::holds_alternative<int64_t>(value)) {
    return std::get<int64_t>(value);
  }
  if (std::holds_alternative<double>(value)) {
    return static_cast<int64_t>(std::get<double>(value));
  }
  return std::nullopt;
}

std::optional<std::string> keyFromValue(const BytecodeValue &value) {
  if (std::holds_alternative<std::string>(value)) {
    return std::get<std::string>(value);
  }
  if (std::holds_alternative<int64_t>(value)) {
    return std::to_string(std::get<int64_t>(value));
  }
  if (std::holds_alternative<double>(value)) {
    std::ostringstream out;
    out << std::get<double>(value);
    return out.str();
  }
  if (std::holds_alternative<bool>(value)) {
    return std::get<bool>(value) ? "true" : "false";
  }
  return std::nullopt;
}
} // namespace

VM::VM() : current_chunk(nullptr) {
  registerDefaultHostFunctions();
}

template <typename T>
T VM::getValue(const BytecodeValue &value) {
  if constexpr (std::is_same_v<T, std::nullptr_t>) {
    return std::get<std::nullptr_t>(value);
  } else if constexpr (std::is_same_v<T, bool>) {
    return std::get<bool>(value);
  } else if constexpr (std::is_same_v<T, int64_t>) {
    return std::get<int64_t>(value);
  } else if constexpr (std::is_same_v<T, double>) {
    return std::get<double>(value);
  } else if constexpr (std::is_same_v<T, std::string>) {
    return std::get<std::string>(value);
  }

  throw std::runtime_error("Invalid type conversion");
}

const VM::CallFrame &
VM::currentFrame() const {
  if (frames.empty()) {
    throw std::runtime_error("No active call frame");
  }
  return frames.back();
}

VM::CallFrame &VM::currentFrame() {
  if (frames.empty()) {
    throw std::runtime_error("No active call frame");
  }
  return frames.back();
}

BytecodeValue VM::getConstant(uint32_t index) {
  return currentFrame().function->constants[index];
}

void VM::registerHostFunction(
    const std::string &name, BytecodeHostFunction function) {
  host_functions[name] = std::move(function);
}

bool VM::hasHostFunction(const std::string &name) const {
  return host_functions.find(name) != host_functions.end();
}

void VM::registerDefaultHostFunctions() {
  registerHostFunction("print", [](const std::vector<BytecodeValue> &args) {
    for (size_t i = 0; i < args.size(); ++i) {
      if (i > 0) {
        std::cout << ' ';
      }
      std::cout << toString(args[i]);
    }
    std::cout << std::endl;
    return BytecodeValue(nullptr);
  });

  registerHostFunction("clock_ms", [](const std::vector<BytecodeValue> &) {
    const auto now =
        std::chrono::time_point_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now())
            .time_since_epoch()
            .count();
    return BytecodeValue(static_cast<int64_t>(now));
  });

  registerHostFunction("sleep_ms", [](const std::vector<BytecodeValue> &args) {
    if (args.size() != 1 || !std::holds_alternative<int64_t>(args[0])) {
      throw std::runtime_error("sleep_ms expects exactly 1 integer argument");
    }

    int64_t duration_ms = std::get<int64_t>(args[0]);
    if (duration_ms < 0) {
      throw std::runtime_error("sleep_ms duration cannot be negative");
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
    return BytecodeValue(nullptr);
  });
}

BytecodeValue VM::invokeHostFunction(const std::string &name,
                                                           uint32_t arg_count) {
  auto it = host_functions.find(name);
  if (it == host_functions.end()) {
    throw std::runtime_error("Host function not found: " + name);
  }

  std::vector<BytecodeValue> args(arg_count);
  for (uint32_t i = 0; i < arg_count; ++i) {
    if (stack.empty()) {
      throw std::runtime_error("Stack underflow while reading host arguments");
    }
    args[arg_count - 1 - i] = stack.top();
    stack.pop();
  }

  return it->second(args);
}

BytecodeValue VM::execute(
    const BytecodeChunk &chunk, const std::string &function_name,
    const std::vector<BytecodeValue> &args) {
  current_chunk = &chunk;

  const auto *entry = chunk.getFunction(function_name);
  if (!entry) {
    throw std::runtime_error("Function not found: " + function_name);
  }

  while (!stack.empty()) {
    stack.pop();
  }
  locals.clear();
  frames.clear();
  closures.clear();
  open_upvalues.clear();
  next_closure_id = 1;
  arrays_.clear();
  objects_.clear();
  sets_.clear();
  next_array_id_ = 1;
  next_object_id_ = 1;
  next_set_id_ = 1;

  frames.push_back(CallFrame{entry, 0, 0, 0});
  locals.resize(entry->local_count);

  if (!args.empty()) {
    if (args.size() != entry->param_count) {
      throw std::runtime_error("Argument count mismatch for entry function '" +
                               function_name + "' (expected " +
                               std::to_string(entry->param_count) + ", got " +
                               std::to_string(args.size()) + ")");
    }

    for (uint32_t i = 0; i < entry->param_count; ++i) {
      locals[i] = args[i];
    }
  }

  if (debug_mode) {
    std::cout << "=== Executing function: " << function_name << " ==="
              << std::endl;
  }

  while (!frames.empty()) {
    auto *active_frame = &currentFrame();
    size_t previous_ip = active_frame->ip;

    auto &frame = *active_frame;
    if (frame.ip >= frame.function->instructions.size()) {
      stack.push(nullptr);
      executeInstruction(Instruction{OpCode::RETURN});
      continue;
    }

    const auto &instruction = frame.function->instructions[frame.ip];

    if (debug_mode) {
      std::cout << "IP: " << frame.ip
                << " OP: " << static_cast<int>(instruction.opcode)
                << std::endl;
    }

    executeInstruction(instruction);
    if (!frames.empty() && active_frame == &currentFrame() &&
        currentFrame().ip == previous_ip) {
      currentFrame().ip++;
    }
  }

  if (stack.empty()) {
    return nullptr;
  }

  BytecodeValue result = stack.top();
  stack.pop();
  return result;
}

void VM::setDebugMode(bool enabled) {
  debug_mode = enabled;
}

void VM::doCall(BytecodeValue callee_value, std::vector<BytecodeValue> args) {
  uint32_t function_index = 0;
  uint32_t closure_id = 0;
  if (std::holds_alternative<FunctionObject>(callee_value)) {
    function_index = std::get<FunctionObject>(callee_value).function_index;
  } else if (std::holds_alternative<ClosureRef>(callee_value)) {
    closure_id = std::get<ClosureRef>(callee_value).id;
    auto closure_it = closures.find(closure_id);
    if (closure_it == closures.end()) {
      throw std::runtime_error("Closure not found: " + std::to_string(closure_id));
    }
    function_index = closure_it->second.function_index;
  } else {
    throw std::runtime_error("CALL expects function or closure as callee");
  }

  const auto *callee = current_chunk->getFunction(function_index);
  if (!callee) {
    throw std::runtime_error("Function index not found: " +
                             std::to_string(function_index));
  }

  if (args.size() != callee->param_count) {
    throw std::runtime_error("Argument count mismatch calling function index " +
                             std::to_string(function_index) +
                             " (expected " +
                             std::to_string(callee->param_count) + ", got " +
                             std::to_string(args.size()) + ")");
  }

  // Advance caller IP now so RETURN resumes at the next instruction.
  currentFrame().ip++;

  size_t base = locals.size();
  locals.resize(base + callee->local_count, nullptr);
  frames.push_back(CallFrame{callee, 0, base, closure_id});

  for (uint32_t i = 0; i < args.size(); i++) {
    locals[base + i] = std::move(args[i]);
  }
}

void VM::closeFrameUpvalues(uint32_t locals_base, uint32_t locals_end) {
  if (locals_end < locals_base) {
    return;
  }

  std::vector<uint32_t> to_close;
  to_close.reserve(open_upvalues.size());
  for (const auto &[index, _] : open_upvalues) {
    if (index >= locals_base && index < locals_end) {
      to_close.push_back(index);
    }
  }

  for (uint32_t index : to_close) {
    auto it = open_upvalues.find(index);
    if (it == open_upvalues.end() || !it->second) {
      continue;
    }
    auto &cell = it->second;
    if (index < locals.size()) {
      cell->closed_value = locals[index];
    } else {
      cell->closed_value = nullptr;
    }
    cell->is_open = false;
    open_upvalues.erase(it);
  }
}

void VM::executeInstruction(
    const Instruction &instruction) {
  auto pop = [this]() -> BytecodeValue {
    if (stack.empty()) {
      throw std::runtime_error("Stack underflow");
    }

    BytecodeValue value = stack.top();
    stack.pop();
    return value;
  };

  auto push = [this](BytecodeValue value) {
    stack.push(std::move(value));
  };

  auto toAbsoluteLocal = [this](uint32_t local_index) -> uint32_t {
    return static_cast<uint32_t>(currentFrame().locals_base + local_index);
  };

  auto ensureLocalIndex = [this](uint32_t absolute_index) {
    if (absolute_index >= locals.size()) {
      locals.resize(static_cast<size_t>(absolute_index) + 1, nullptr);
    }
  };

  auto doReturn = [this, &pop, &push]() {
    if (frames.empty()) {
      return;
    }

    BytecodeValue ret = nullptr;
    if (!stack.empty()) {
      ret = pop();
    }

    auto finished = frames.back();
    frames.pop_back();

    closeFrameUpvalues(static_cast<uint32_t>(finished.locals_base),
                       static_cast<uint32_t>(locals.size()));

    if (locals.size() >= finished.locals_base) {
      locals.resize(finished.locals_base);
    }

    push(ret);
  };

  switch (instruction.opcode) {
  case OpCode::LOAD_CONST: {
    uint32_t const_index = std::get<uint32_t>(instruction.operands[0]);
    push(getConstant(const_index));
    break;
  }

  case OpCode::LOAD_VAR: {
    uint32_t var_index = std::get<uint32_t>(instruction.operands[0]);
    uint32_t abs = toAbsoluteLocal(var_index);
    ensureLocalIndex(abs);
    push(locals[abs]);
    break;
  }

  case OpCode::STORE_VAR: {
    uint32_t var_index = std::get<uint32_t>(instruction.operands[0]);
    uint32_t abs = toAbsoluteLocal(var_index);
    ensureLocalIndex(abs);
    locals[abs] = pop();
    break;
  }

  case OpCode::LOAD_UPVALUE: {
    uint32_t upvalue_index = std::get<uint32_t>(instruction.operands[0]);
    uint32_t closure_id = currentFrame().closure_id;
    if (closure_id == 0) {
      throw std::runtime_error("LOAD_UPVALUE used without active closure");
    }
    auto closure_it = closures.find(closure_id);
    if (closure_it == closures.end()) {
      throw std::runtime_error("Closure not found for LOAD_UPVALUE");
    }
    if (upvalue_index >= closure_it->second.upvalues.size() ||
        !closure_it->second.upvalues[upvalue_index]) {
      throw std::runtime_error("LOAD_UPVALUE index out of range");
    }
    const auto &cell = closure_it->second.upvalues[upvalue_index];
    if (cell->is_open) {
      ensureLocalIndex(cell->open_index);
      push(locals[cell->open_index]);
    } else {
      push(cell->closed_value);
    }
    break;
  }

  case OpCode::STORE_UPVALUE: {
    uint32_t upvalue_index = std::get<uint32_t>(instruction.operands[0]);
    uint32_t closure_id = currentFrame().closure_id;
    if (closure_id == 0) {
      throw std::runtime_error("STORE_UPVALUE used without active closure");
    }
    auto closure_it = closures.find(closure_id);
    if (closure_it == closures.end()) {
      throw std::runtime_error("Closure not found for STORE_UPVALUE");
    }
    if (upvalue_index >= closure_it->second.upvalues.size() ||
        !closure_it->second.upvalues[upvalue_index]) {
      throw std::runtime_error("STORE_UPVALUE index out of range");
    }
    auto &cell = closure_it->second.upvalues[upvalue_index];
    BytecodeValue value = pop();
    if (cell->is_open) {
      ensureLocalIndex(cell->open_index);
      locals[cell->open_index] = value;
    } else {
      cell->closed_value = value;
    }
    break;
  }

  case OpCode::POP: {
    pop();
    break;
  }

  case OpCode::DUP: {
    BytecodeValue value = pop();
    push(value);
    push(value);
    break;
  }

  case OpCode::SWAP: {
    BytecodeValue top = pop();
    BytecodeValue next = pop();
    push(top);
    push(next);
    break;
  }

  case OpCode::ADD:
  case OpCode::SUB:
  case OpCode::MUL:
  case OpCode::DIV:
  case OpCode::MOD:
  case OpCode::POW:
  case OpCode::EQ:
  case OpCode::NEQ:
  case OpCode::LT:
  case OpCode::LTE:
  case OpCode::GT:
  case OpCode::GTE: {
    BytecodeValue right = pop();
    BytecodeValue left = pop();

    if (std::holds_alternative<int64_t>(left) &&
        std::holds_alternative<int64_t>(right)) {
      int64_t l = std::get<int64_t>(left);
      int64_t r = std::get<int64_t>(right);
      switch (instruction.opcode) {
      case OpCode::ADD:
        push(l + r);
        break;
      case OpCode::SUB:
        push(l - r);
        break;
      case OpCode::MUL:
        push(l * r);
        break;
      case OpCode::DIV:
        if (r == 0) {
          throw std::runtime_error("Division by zero");
        }
        push(l / r);
        break;
      case OpCode::MOD:
        if (r == 0) {
          throw std::runtime_error("Modulo by zero");
        }
        push(l % r);
        break;
      case OpCode::EQ:
        push(l == r);
        break;
      case OpCode::NEQ:
        push(l != r);
        break;
      case OpCode::LT:
        push(l < r);
        break;
      case OpCode::LTE:
        push(l <= r);
        break;
      case OpCode::GT:
        push(l > r);
        break;
      case OpCode::GTE:
        push(l >= r);
        break;
      default:
        throw std::runtime_error("Unsupported integer operation");
      }
      break;
    }

    if ((std::holds_alternative<int64_t>(left) ||
         std::holds_alternative<double>(left)) &&
        (std::holds_alternative<int64_t>(right) ||
         std::holds_alternative<double>(right))) {
      double l = std::holds_alternative<int64_t>(left)
                     ? static_cast<double>(std::get<int64_t>(left))
                     : std::get<double>(left);
      double r = std::holds_alternative<int64_t>(right)
                     ? static_cast<double>(std::get<int64_t>(right))
                     : std::get<double>(right);

      switch (instruction.opcode) {
      case OpCode::ADD:
        push(l + r);
        break;
      case OpCode::SUB:
        push(l - r);
        break;
      case OpCode::MUL:
        push(l * r);
        break;
      case OpCode::DIV:
        if (r == 0.0) {
          throw std::runtime_error("Division by zero");
        }
        push(l / r);
        break;
      case OpCode::MOD:
        if (r == 0.0) {
          throw std::runtime_error("Modulo by zero");
        }
        push(std::fmod(l, r));
        break;
      case OpCode::POW:
        push(std::pow(l, r));
        break;
      case OpCode::EQ:
        push(l == r);
        break;
      case OpCode::NEQ:
        push(l != r);
        break;
      case OpCode::LT:
        push(l < r);
        break;
      case OpCode::LTE:
        push(l <= r);
        break;
      case OpCode::GT:
        push(l > r);
        break;
      case OpCode::GTE:
        push(l >= r);
        break;
      default:
        throw std::runtime_error("Unsupported floating point operation");
      }
      break;
    }

    if (std::holds_alternative<std::string>(left) &&
        std::holds_alternative<std::string>(right)) {
      const std::string &l = std::get<std::string>(left);
      const std::string &r = std::get<std::string>(right);

      switch (instruction.opcode) {
      case OpCode::ADD:
        push(l + r);
        break;
      case OpCode::EQ:
        push(l == r);
        break;
      case OpCode::NEQ:
        push(l != r);
        break;
      default:
        throw std::runtime_error("Invalid string operation");
      }
      break;
    }

    throw std::runtime_error("Type mismatch in binary operation");
  }

  case OpCode::AND: {
    BytecodeValue right = pop();
    BytecodeValue left = pop();
    push(getValue<bool>(left) && getValue<bool>(right));
    break;
  }

  case OpCode::OR: {
    BytecodeValue right = pop();
    BytecodeValue left = pop();
    push(getValue<bool>(left) || getValue<bool>(right));
    break;
  }

  case OpCode::NOT: {
    BytecodeValue value = pop();
    push(!getValue<bool>(value));
    break;
  }

  case OpCode::JUMP: {
    uint32_t target = std::get<uint32_t>(instruction.operands[0]);
    currentFrame().ip = target;
    break;
  }

  case OpCode::JUMP_IF_FALSE: {
    uint32_t target = std::get<uint32_t>(instruction.operands[0]);
    BytecodeValue condition = pop();
    if (!getValue<bool>(condition)) {
      currentFrame().ip = target;
    }
    break;
  }

  case OpCode::JUMP_IF_TRUE: {
    uint32_t target = std::get<uint32_t>(instruction.operands[0]);
    BytecodeValue condition = pop();
    if (getValue<bool>(condition)) {
      currentFrame().ip = target;
    }
    break;
  }

  case OpCode::CALL: {
    uint32_t arg_count = std::get<uint32_t>(instruction.operands[0]);
    if (stack.size() < static_cast<size_t>(arg_count) + 1) {
      throw std::runtime_error("Stack underflow during CALL");
    }

    std::vector<BytecodeValue> args(arg_count);
    for (uint32_t i = 0; i < arg_count; ++i) {
      args[arg_count - 1 - i] = pop();
    }
    BytecodeValue callee_value = pop();

    doCall(callee_value, std::move(args));
    break;
  }

  case OpCode::CALL_HOST: {
    if (instruction.operands.size() != 2 ||
        !std::holds_alternative<std::string>(instruction.operands[0]) ||
        !std::holds_alternative<uint32_t>(instruction.operands[1])) {
      throw std::runtime_error(
          "CALL_HOST expects operands: <string function_name, uint32 arg_count>");
    }

    const std::string &function_name =
        std::get<std::string>(instruction.operands[0]);
    uint32_t arg_count = std::get<uint32_t>(instruction.operands[1]);
    push(invokeHostFunction(function_name, arg_count));
    break;
  }

  case OpCode::RETURN: {
    doReturn();
    break;
  }

  case OpCode::CLOSURE: {
    uint32_t function_index = std::get<uint32_t>(instruction.operands[0]);
    const auto *target = current_chunk->getFunction(function_index);
    if (!target) {
      throw std::runtime_error("CLOSURE references unknown function index");
    }

    RuntimeClosure closure;
    closure.function_index = function_index;
    closure.upvalues.reserve(target->upvalues.size());
    for (const auto &descriptor : target->upvalues) {
      if (descriptor.captures_local) {
        uint32_t abs = toAbsoluteLocal(descriptor.index);
        ensureLocalIndex(abs);
        auto open_it = open_upvalues.find(abs);
        if (open_it == open_upvalues.end()) {
          auto cell = std::make_shared<RuntimeClosure::UpvalueCell>();
          cell->is_open = true;
          cell->open_index = abs;
          open_upvalues.emplace(abs, cell);
          closure.upvalues.push_back(std::move(cell));
        } else {
          closure.upvalues.push_back(open_it->second);
        }
      } else {
        uint32_t parent_closure_id = currentFrame().closure_id;
        if (parent_closure_id == 0) {
          throw std::runtime_error(
              "CLOSURE tried to capture upvalue without parent closure");
        }
        auto parent_it = closures.find(parent_closure_id);
        if (parent_it == closures.end()) {
          throw std::runtime_error("Parent closure not found for CLOSURE");
        }
        if (descriptor.index >= parent_it->second.upvalues.size()) {
          throw std::runtime_error("CLOSURE upvalue index out of range");
        }
        closure.upvalues.push_back(parent_it->second.upvalues[descriptor.index]);
      }
    }

    uint32_t closure_id = next_closure_id++;
    closures.emplace(closure_id, std::move(closure));
    push(ClosureRef{.id = closure_id});
    break;
  }

  case OpCode::ARRAY_NEW: {
    uint32_t id = next_array_id_++;
    arrays_[id] = {};
    push(ArrayRef{.id = id});
    break;
  }

  case OpCode::SET_NEW: {
    uint32_t id = next_set_id_++;
    sets_[id] = {};
    push(SetRef{.id = id});
    break;
  }

  case OpCode::ARRAY_PUSH: {
    BytecodeValue value = pop();
    BytecodeValue container = pop();
    if (!std::holds_alternative<ArrayRef>(container)) {
      throw std::runtime_error("ARRAY_PUSH expects array container");
    }
    uint32_t id = std::get<ArrayRef>(container).id;
    auto it = arrays_.find(id);
    if (it == arrays_.end()) {
      throw std::runtime_error("ARRAY_PUSH unknown array id");
    }
    it->second.push_back(value);
    push(container);
    break;
  }

  case OpCode::ARRAY_GET: {
    BytecodeValue index_or_key = pop();
    BytecodeValue container = pop();

    if (std::holds_alternative<ArrayRef>(container)) {
      auto index = indexFromValue(index_or_key);
      if (!index || *index < 0) {
        throw std::runtime_error("ARRAY_GET expects non-negative integer index");
      }
      auto it = arrays_.find(std::get<ArrayRef>(container).id);
      if (it == arrays_.end()) {
        throw std::runtime_error("ARRAY_GET unknown array id");
      }
      if (static_cast<size_t>(*index) >= it->second.size()) {
        push(nullptr);
      } else {
        push(it->second[static_cast<size_t>(*index)]);
      }
      break;
    }

    if (std::holds_alternative<SetRef>(container)) {
      auto key = keyFromValue(index_or_key);
      if (!key) {
        throw std::runtime_error("SET membership expects string/number/bool key");
      }
      auto it = sets_.find(std::get<SetRef>(container).id);
      if (it == sets_.end()) {
        throw std::runtime_error("ARRAY_GET unknown set id");
      }
      push(it->second.find(*key) != it->second.end());
      break;
    }

    if (std::holds_alternative<ObjectRef>(container)) {
      auto key = keyFromValue(index_or_key);
      if (!key) {
        throw std::runtime_error("OBJECT index expects string/number/bool key");
      }
      auto it = objects_.find(std::get<ObjectRef>(container).id);
      if (it == objects_.end()) {
        throw std::runtime_error("ARRAY_GET unknown object id");
      }
      auto kv = it->second.find(*key);
      push(kv == it->second.end() ? BytecodeValue(nullptr) : kv->second);
      break;
    }

    throw std::runtime_error("ARRAY_GET expects array/set/object container");
  }

  case OpCode::ARRAY_SET: {
    BytecodeValue value = pop();
    BytecodeValue index_or_key = pop();
    BytecodeValue container = pop();

    if (std::holds_alternative<ArrayRef>(container)) {
      auto index = indexFromValue(index_or_key);
      if (!index || *index < 0) {
        throw std::runtime_error("ARRAY_SET expects non-negative integer index");
      }
      auto it = arrays_.find(std::get<ArrayRef>(container).id);
      if (it == arrays_.end()) {
        throw std::runtime_error("ARRAY_SET unknown array id");
      }
      const auto idx = static_cast<size_t>(*index);
      if (idx >= it->second.size()) {
        it->second.resize(idx + 1, nullptr);
      }
      it->second[idx] = value;
      break;
    }

    if (std::holds_alternative<SetRef>(container)) {
      auto key = keyFromValue(index_or_key);
      if (!key) {
        throw std::runtime_error("SET assignment expects string/number/bool key");
      }
      auto it = sets_.find(std::get<SetRef>(container).id);
      if (it == sets_.end()) {
        throw std::runtime_error("ARRAY_SET unknown set id");
      }
      bool present = false;
      if (std::holds_alternative<bool>(value)) {
        present = std::get<bool>(value);
      } else if (std::holds_alternative<int64_t>(value)) {
        present = std::get<int64_t>(value) != 0;
      } else if (std::holds_alternative<double>(value)) {
        present = std::get<double>(value) != 0.0;
      } else {
        throw std::runtime_error(
            "SET assignment value must be bool/number to indicate presence");
      }
      if (present) {
        it->second[*key] = nullptr;
      } else {
        it->second.erase(*key);
      }
      break;
    }

    if (std::holds_alternative<ObjectRef>(container)) {
      auto key = keyFromValue(index_or_key);
      if (!key) {
        throw std::runtime_error("OBJECT index assignment expects valid key");
      }
      auto it = objects_.find(std::get<ObjectRef>(container).id);
      if (it == objects_.end()) {
        throw std::runtime_error("ARRAY_SET unknown object id");
      }
      it->second[*key] = value;
      break;
    }

    throw std::runtime_error("ARRAY_SET expects array/set/object container");
  }

  case OpCode::OBJECT_NEW: {
    uint32_t id = next_object_id_++;
    objects_[id] = {};
    push(ObjectRef{.id = id});
    break;
  }

  case OpCode::OBJECT_GET: {
    BytecodeValue key_value = pop();
    BytecodeValue object = pop();
    if (!std::holds_alternative<ObjectRef>(object)) {
      throw std::runtime_error("OBJECT_GET expects object container");
    }
    auto key = keyFromValue(key_value);
    if (!key) {
      throw std::runtime_error("OBJECT_GET expects string/number/bool key");
    }
    auto it = objects_.find(std::get<ObjectRef>(object).id);
    if (it == objects_.end()) {
      throw std::runtime_error("OBJECT_GET unknown object id");
    }
    auto kv = it->second.find(*key);
    push(kv == it->second.end() ? BytecodeValue(nullptr) : kv->second);
    break;
  }

  case OpCode::OBJECT_SET: {
    std::optional<std::string> key;
    BytecodeValue value = nullptr;
    BytecodeValue object = nullptr;
    if (!instruction.operands.empty()) {
      value = pop();
      object = pop();
      key = keyFromValue(instruction.operands[0]);
    } else {
      value = pop();
      BytecodeValue key_value = pop();
      object = pop();
      key = keyFromValue(key_value);
    }

    if (!std::holds_alternative<ObjectRef>(object)) {
      throw std::runtime_error("OBJECT_SET expects object container");
    }

    if (!key) {
      throw std::runtime_error("OBJECT_SET expects string/number/bool key");
    }
    auto it = objects_.find(std::get<ObjectRef>(object).id);
    if (it == objects_.end()) {
      throw std::runtime_error("OBJECT_SET unknown object id");
    }
    it->second[*key] = value;
    break;
  }

  case OpCode::PRINT: {
    BytecodeValue value = pop();
    std::cout << toString(value) << std::endl;
    break;
  }

  case OpCode::DEBUG: {
    std::cout << "DEBUG: Stack size: " << stack.size() << std::endl;
    std::cout << "DEBUG: Locals size: " << locals.size() << std::endl;
    break;
  }

  case OpCode::NOP:
  case OpCode::DEFINE_FUNC:
    break;

  default:
    throw std::runtime_error("Unknown opcode: " +
                             std::to_string(static_cast<int>(instruction.opcode)));
  }
}

std::unique_ptr<BytecodeInterpreter> createVM() {
  return std::make_unique<VM>();
}

} // namespace havel::compiler
