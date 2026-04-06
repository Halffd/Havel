#include "VM.hpp"
#include "../../utils/ErrorPrinter.hpp"
#include "../../errors/ErrorSystem.h"
#include "../../runtime/concurrency/Thread.hpp"

#include "../../../core/io/MouseController.hpp" // For ParseDuration
#include <chrono>
#include <cmath>
#include <iostream>
#include <mutex>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <fstream>

// Helper macro for throwing runtime errors
#define COMPILER_THROW(msg) throw std::runtime_error(msg)

namespace havel::compiler {

// ============================================================================
// ScriptError conversion to unified error system (TEMPORARILY DISABLED for Qt moc compatibility)
// ============================================================================

/*
::havel::errors::HavelError ScriptError::toHavelError() const {
  ::havel::errors::HavelError err(::havel::errors::ErrorSeverity::Error,
                                 ::havel::errors::ErrorStage::VM,
                                 message);
  err.at(line, column);
  
  // Parse stack trace if available
  if (!stackTrace.empty()) {
    std::vector<::havel::errors::StackFrame> frames;
    std::istringstream iss(stackTrace);
    std::string lineStr;
    while (std::getline(iss, lineStr)) {
      ::havel::errors::StackFrame frame;
      frame.functionName = lineStr; // Simplified - could parse better format
      frames.push_back(std::move(frame));
    }
    err.withStackTrace(std::move(frames));
  }
  
  return err;
}
*/

namespace {
// Helper function to compare two Values for equality
static bool valuesEqual(const Value &a, const Value &b) {
  // Type mismatch check
  if (a.isNull() != b.isNull()) return false;
  if (a.isBool() != b.isBool()) return false;
  if (a.isInt() != b.isInt()) return false;
  if (a.isDouble() != b.isDouble()) return false;
  if (a.isStringValId() != b.isStringValId()) return false;
  if (a.isArrayId() != b.isArrayId()) return false;
  if (a.isObjectId() != b.isObjectId()) return false;
  if (a.isRangeId() != b.isRangeId()) return false;

  if (a.isNull()) return true;
  if (a.isBool()) return a.asBool() == b.asBool();
  if (a.isInt()) return a.asInt() == b.asInt();
  if (a.isDouble()) return a.asDouble() == b.asDouble();
  if (a.isStringValId()) return a.asStringValId() == b.asStringValId();

  // For reference types, compare IDs
  if (a.isArrayId()) {
    return a.asArrayId() == b.asArrayId();
  }
  if (a.isObjectId()) {
    return a.asObjectId() == b.asObjectId();
  }
  if (a.isRangeId()) {
    return a.asRangeId() == b.asRangeId();
  }

  return false;
}

struct ScriptThrow final {
  Value value;
};

// Internal toString with depth limit only (no cycle detection - confuses users)
} // anonymous namespace


std::string VM::toString(const Value &value) const {
  std::unordered_set<uint32_t> visited;
  return toStringInternal(value, visited, 0);
}

std::string VM::toStringInternal(const Value &value, std::unordered_set<uint32_t> &visitedIds, int depth) const {
  if (depth > 8) return "...";

  if (value.isNull()) return "null";
  if (value.isBool()) return value.asBool() ? "true" : "false";
  if (value.isInt()) return std::to_string(value.asInt());
  if (value.isDouble()) {
    std::ostringstream out;
    out << value.asDouble();
    return out.str();
  }
  if (value.isStringValId()) {
    if (current_chunk) {
      return current_chunk->getString(value.asStringValId());
    }
    return "<string:" + std::to_string(value.asStringValId()) + ">";
  }
  if (value.isStringId()) {
    if (auto *s = heap_.string(value.asStringId())) {
      return *s;
    }
    return "<string:" + std::to_string(value.asStringId()) + ">";
  }
  if (value.isFunctionObjId()) {
    return "fn[" + std::to_string(value.asFunctionObjId()) + "]";
  }
  if (value.isClosureId()) {
    return "closure[" + std::to_string(value.asClosureId()) + "]";
  }
  if (value.isArrayId()) {
    auto *arr = heap_.array(value.asArrayId());
    if (!arr) return "[]";
    std::string result = "[";
    for (size_t i = 0; i < arr->size(); ++i) {
      if (i > 0) result += ", ";
      result += toStringInternal((*arr)[i], visitedIds, depth + 1);
    }
    result += "]";
    return result;
  }
  if (value.isObjectId()) {
    auto *obj = heap_.object(value.asObjectId());
    if (!obj) return "{}";
    std::string result = "{";
    bool first = true;
    for (const auto &[key, val] : *obj) {
      if (!first) result += ", ";
      first = false;
      result += "\"" + key + "\": " + toStringInternal(val, visitedIds, depth + 1);
    }
    result += "}";
    return result;
  }
  return "unknown";
}

// Type conversion helpers
int64_t VM::toInt(const Value &value) const {
  if (value.isInt()) {
    return value.asInt();
  }
  if (value.isDouble()) {
    return static_cast<int64_t>(value.asDouble());
  }
  if (value.isBool()) {
    return value.asBool() ? 1 : 0;
  }
  if (value.isStringValId()) {
    if (current_chunk) {
      try {
        return std::stoll(current_chunk->getString(value.asStringValId()));
      } catch (...) {
        return 0;
      }
    }
  }
  return 0;
}

double VM::toFloat(const Value &value) const {
  if (value.isDouble()) {
    return value.asDouble();
  }
  if (value.isInt()) {
    return static_cast<double>(value.asInt());
  }
  if (value.isBool()) {
    return value.asBool() ? 1.0 : 0.0;
  }
  if (value.isStringValId()) {
    if (current_chunk) {
      try {
        return std::stod(current_chunk->getString(value.asStringValId()));
      } catch (...) {
        return 0.0;
      }
    }
  }
  return 0.0;
}

bool VM::toBool(const Value &value) const {
  if (value.isBool()) {
    return value.asBool();
  }
  if (value.isInt()) {
    return value.asInt() != 0;
  }
  if (value.isDouble()) {
    return value.asDouble() != 0.0;
  }
  if (value.isStringValId()) {
    if (current_chunk) {
      return !current_chunk->getString(value.asStringValId()).empty();
    }
    return true; // nonempty string is truthy
  }
  // Collections: JavaScript truthiness (all collections are truthy, even empty)
  if (value.isArrayId() || value.isObjectId() || value.isSetId()) {
    return true;
  }
  return !value.isNull();
}

std::optional<std::string> VM::valueAsString(const Value &value) const {
  if (value.isStringValId()) {
    if (!current_chunk) {
      return std::nullopt;
    }
    return current_chunk->getString(value.asStringValId());
  }
  if (value.isStringId()) {
    if (auto *s = heap_.string(value.asStringId())) {
      return *s;
    }
    return std::nullopt;
  }
  return std::nullopt;
}

bool VM::valuesEqualDeep(const Value &left, const Value &right) const {
  std::unordered_set<uint64_t> visited_array_pairs;
  std::unordered_set<uint64_t> visited_object_pairs;
  return valuesEqualDeep(left, right, visited_array_pairs, visited_object_pairs);
}

bool VM::valuesEqualDeep(
    const Value &left, const Value &right,
    std::unordered_set<uint64_t> &visited_array_pairs,
    std::unordered_set<uint64_t> &visited_object_pairs) const {
  if (left.isNull() || right.isNull()) {
    return left.isNull() && right.isNull();
  }

  if ((left.isInt() || left.isDouble()) && (right.isInt() || right.isDouble())) {
    const double l = left.isInt() ? static_cast<double>(left.asInt()) : left.asDouble();
    const double r = right.isInt() ? static_cast<double>(right.asInt()) : right.asDouble();
    return l == r;
  }

  if (left.isBool() && right.isBool()) {
    return left.asBool() == right.asBool();
  }

  if (auto l = valueAsString(left); l.has_value()) {
    auto r = valueAsString(right);
    return r.has_value() && (*l == *r);
  }
  if (left.isStringValId() || left.isStringId() || right.isStringValId() ||
      right.isStringId()) {
    return false;
  }

  if (left.isArrayId() && right.isArrayId()) {
    const uint32_t l_id = left.asArrayId();
    const uint32_t r_id = right.asArrayId();
    if (l_id == r_id) {
      return true;
    }

    const uint32_t lo = std::min(l_id, r_id);
    const uint32_t hi = std::max(l_id, r_id);
    const uint64_t pair = (static_cast<uint64_t>(lo) << 32) | hi;
    if (!visited_array_pairs.insert(pair).second) {
      return true;
    }

    const auto *l_arr = heap_.array(l_id);
    const auto *r_arr = heap_.array(r_id);
    if (!l_arr || !r_arr) {
      return false;
    }
    if (l_arr->size() != r_arr->size()) {
      return false;
    }

    for (size_t i = 0; i < l_arr->size(); i++) {
      if (!valuesEqualDeep((*l_arr)[i], (*r_arr)[i], visited_array_pairs,
                           visited_object_pairs)) {
        return false;
      }
    }
    return true;
  }

  if (left.isObjectId() && right.isObjectId()) {
    const uint32_t l_id = left.asObjectId();
    const uint32_t r_id = right.asObjectId();
    if (l_id == r_id) {
      return true;
    }

    const uint32_t lo = std::min(l_id, r_id);
    const uint32_t hi = std::max(l_id, r_id);
    const uint64_t pair = (static_cast<uint64_t>(lo) << 32) | hi;
    if (!visited_object_pairs.insert(pair).second) {
      return true;
    }

    const auto *l_obj = heap_.object(l_id);
    const auto *r_obj = heap_.object(r_id);
    if (!l_obj || !r_obj) {
      return false;
    }
    if (l_obj->size() != r_obj->size()) {
      return false;
    }

    for (const auto &[key, l_value] : *l_obj) {
      auto it = r_obj->find(key);
      if (it == r_obj->end()) {
        return false;
      }
      if (!valuesEqualDeep(l_value, it->second, visited_array_pairs,
                           visited_object_pairs)) {
        return false;
      }
    }
    return true;
  }

  if (left.isRangeId() && right.isRangeId()) {
    return left.asRangeId() == right.asRangeId();
  }
  if (left.isStructId() && right.isStructId()) {
    return left.asStructId() == right.asStructId();
  }
  if (left.isClassId() && right.isClassId()) {
    return left.asClassId() == right.asClassId();
  }
  if (left.isClosureId() && right.isClosureId()) {
    return left.asClosureId() == right.asClosureId();
  }
  if (left.isFunctionObjId() && right.isFunctionObjId()) {
    return left.asFunctionObjId() == right.asFunctionObjId();
  }
  if (left.isHostFuncId() && right.isHostFuncId()) {
    return left.asHostFuncId() == right.asHostFuncId();
  }

  return false;
}

std::optional<int64_t> indexFromValue(const Value &value) {
  if (value.isInt()) {
    return value.asInt();
  }
  if (value.isDouble()) {
    return static_cast<int64_t>(value.asDouble());
  }
  return std::nullopt;
}

std::optional<std::string> keyFromValue(const Value &value, const GCHeap *heap = nullptr, const BytecodeChunk *chunk = nullptr) {
  if (value.isStringId()) {
    if (heap) {
      if (auto *s = heap->string(value.asStringId())) {
        return *s;
      }
    }
    return "<string:" + std::to_string(value.asStringId()) + ">";
  }
  if (value.isStringValId()) {
    if (chunk) {
      return chunk->getString(value.asStringValId());
    }
    return "<string:" + std::to_string(value.asStringValId()) + ">";
  }
  if (value.isInt()) {
    return std::to_string(value.asInt());
  }
  if (value.isDouble()) {
    std::ostringstream out;
    out << value.asDouble();
    return out.str();
  }
  if (value.isBool()) {
    return value.asBool() ? "true" : "false";
  }
  return std::nullopt;
}

std::string formatSourceLocation(const BytecodeFunction &function, size_t ip) {
  if (ip >= function.instruction_locations.size()) {
    return "<unknown>";
  }
  const auto &location = function.instruction_locations[ip];
  if (location.line == 0 && location.column == 0) {
    return "<unknown>";
  }
  return std::to_string(location.line) + ":" + std::to_string(location.column);
}

SourceLocation nearestSourceLocation(const BytecodeFunction &function,
                                     size_t ip) {
  if (function.instruction_locations.empty()) {
    return {};
  }
  size_t idx = std::min(ip, function.instruction_locations.size() - 1);
  while (true) {
    const auto &loc = function.instruction_locations[idx];
    if (loc.line > 0) {
      return loc;
    }
    if (idx == 0) {
      break;
    }
    --idx;
  }
  return {};
}

std::string VM::formatErrorWithContext(const std::string &message) const {
  if (frame_count_ == 0 || !frame_arena_[frame_count_ - 1].function) {
    return "\033[1;31merror\033[0m: " + message + "\n";
  }

  const auto &frame = frame_arena_[frame_count_ - 1];
  const auto *function = frame.function;

  if (frame.ip >= function->instruction_locations.size()) {
    return "\033[1;31merror\033[0m: " + message + "\n";
  }

  const auto &loc = function->instruction_locations[frame.ip];
  if (loc.line == 0) {
    return "\033[1;31merror\033[0m: " + message + "\n";
  }

  return ::havel::ErrorPrinter::formatErrorFromFile("Runtime Error", message, loc.filename, (size_t)loc.line, (size_t)loc.column, (size_t)loc.length);
}

VM::VM() { registerDefaultHostFunctions(); }

VM::VM(const ::havel::HostContext &ctx) {
  // Store context for service access
  context_ = &ctx;
  registerDefaultHostFunctions();
}

void VM::setMaxCallDepth(size_t value) { max_call_depth_ = value; }

VM::~VM() {
  if (heap_.externalRootCount() > 0) {
    std::cerr << "[VM][GC] Warning: " << heap_.externalRootCount()
              << " external roots still pinned at VM shutdown" << std::endl;
  }
}

template <typename T> T VM::getValue(const Value &value) {
  if constexpr (std::is_same_v<T, std::nullptr_t>) {
    return nullptr;
  } else if constexpr (std::is_same_v<T, bool>) {
    return value.asBool();
  } else if constexpr (std::is_same_v<T, int64_t>) {
    return value.asInt();
  } else if constexpr (std::is_same_v<T, double>) {
    return value.asDouble();
  } else if constexpr (std::is_same_v<T, std::string>) {
    // TODO: string pool lookup
    return "<string:" + std::to_string(value.asStringValId()) + ">";
  }

  COMPILER_THROW("Invalid type conversion");
}

const VM::CallFrame &VM::currentFrame() const {
  if (frame_count_ == 0) {
    COMPILER_THROW("No active call frame");
  }
  return frame_arena_[frame_count_ - 1];
}

VM::CallFrame &VM::currentFrame() {
  if (frame_count_ == 0) {
    COMPILER_THROW("No active call frame");
  }
  return frame_arena_[frame_count_ - 1];
}

Value VM::getConstant(uint32_t index) {
  return currentFrame().function->constants[index];
}

VM::ExecutionState VM::saveState() const {
  ExecutionState state;
  state.stack = stack;
  state.locals = locals;
  state.frames = frame_arena_;
  state.frame_count = frame_count_;
  return state;
}

void VM::restoreState(const ExecutionState &state) {
  stack = state.stack;
  locals = state.locals;
  frame_arena_ = state.frames;
  frame_count_ = state.frame_count;
}

void VM::scheduleCall(const Value &fn,
                      const std::vector<Value> &args,
                      Value &result, bool &completed) {
  pending_calls.push_back({fn, args, &result, &completed});
}

void VM::processPendingCalls() {
  // Process all pending calls - just doCall, let outer loop execute
  for (auto &call : pending_calls) {
    doCall(call.fn, call.args, false);
  }
  pending_calls.clear();
}

// Synchronous call for host functions - executes callback and returns result
// Minimal state isolation: just save/restore stack size
Value VM::callFunctionSync(const Value &fn,
                                   const std::vector<Value> &args) {
  size_t savedStackSize = stack.size();
  size_t savedFrameCount = frame_count_;


  // Execute callback
  doCall(fn, args, false);
  runDispatchLoop(savedFrameCount);


  // Get result from stack top
  Value result;
  if (stack.empty()) {
    result = nullptr;
  } else {
    result = stack.top();
    stack.pop();
  }

  // Just ensure stack is at expected size
  while (stack.size() > savedStackSize) {
    stack.pop();
  }

  return result;
}

void VM::registerHostFunction(const std::string &name,
                              BytecodeHostFunction function) {
  host_functions[name] = std::move(function);
  // Track index for HostFuncId lookup
  uint32_t idx = static_cast<uint32_t>(host_function_names_.size());
  host_function_names_.push_back(name);
  // Register host function by name in globals (for LOAD_GLOBAL lookup)
  // The bytecode will look up the string constant which points to this name
  host_function_globals_[name] = Value::makeHostFuncId(idx);
}

void VM::registerHostFunction(const std::string &name, size_t arity,
                              BytecodeHostFunction function) {
  registerHostFunction(
      name,
      [arity, function = std::move(function),
       name](const std::vector<Value> &args) -> Value {
        if (args.size() != arity) {
          COMPILER_THROW("Host function '" + name + "' expects " +
                                   std::to_string(arity) + " arguments, got " +
                                   std::to_string(args.size()));
        }
        return function(args);
      });
}

bool VM::hasHostFunction(const std::string &name) const {
  return host_functions.find(name) != host_functions.end();
}

uint32_t VM::getHostFunctionIndex(const std::string &name) {
  // Find existing index
  for (uint32_t i = 0; i < host_function_names_.size(); i++) {
    if (host_function_names_[i] == name) {
      return i;
    }
  }
  // Register if not found (shouldn't happen for registered functions)
  uint32_t idx = static_cast<uint32_t>(host_function_names_.size());
  host_function_names_.push_back(name);
  return idx;
}

ObjectRef VM::createHostObject() {
  ObjectRef ref = heap_.allocateObject();
  maybeCollectGarbage();
  return ref;
}

ArrayRef VM::createHostArray() {
  ArrayRef ref = heap_.allocateArray();
  maybeCollectGarbage();
  return ref;
}

StringRef VM::createRuntimeString(std::string value) {
  StringRef ref = heap_.allocateString(std::move(value));
  maybeCollectGarbage();
  return ref;
}

void VM::setHostObjectField(ObjectRef object_ref, const std::string &key,
                            Value value) {
  auto *object = heap_.object(object_ref.id);
  if (!object) {
    COMPILER_THROW("setHostObjectField unknown object id");
  }
  (*object)[key] = std::move(value);
}

void VM::pushHostArrayValue(ArrayRef array_ref, Value value) {
  auto *array = heap_.array(array_ref.id);
  if (!array) {
    COMPILER_THROW("pushHostArrayValue unknown array id");
  }
  array->push_back(std::move(value));
}

// Array helpers
size_t VM::getHostArrayLength(ArrayRef array_ref) {
  auto *array = heap_.array(array_ref.id);
  if (!array)
    return 0;
  return array->size();
}

Value VM::getHostArrayValue(ArrayRef array_ref, size_t index) {
  auto *array = heap_.array(array_ref.id);
  if (!array || index >= array->size())
    return Value::makeNull();
  return (*array)[index];
}

void VM::setHostArrayValue(ArrayRef array_ref, size_t index,
                           Value value) {
  auto *array = heap_.array(array_ref.id);
  if (!array)
    return;
  if (index >= array->size()) {
    // Extend array if needed
    while (array->size() <= index) {
      array->push_back(Value::makeNull());
    }
  }
  (*array)[index] = std::move(value);
}

Value VM::popHostArrayValue(ArrayRef array_ref) {
  auto *array = heap_.array(array_ref.id);
  if (!array || array->empty())
    return Value::makeNull();
  auto value = std::move(array->back());
  array->pop_back();
  return value;
}

void VM::insertHostArrayValue(ArrayRef array_ref, size_t index,
                              Value value) {
  auto *array = heap_.array(array_ref.id);
  if (!array)
    return;
  if (index > array->size())
    index = array->size();
  array->insert(array->begin() + index, std::move(value));
}

Value VM::removeHostArrayValue(ArrayRef array_ref, size_t index) {
  auto *array = heap_.array(array_ref.id);
  if (!array || index >= array->size())
    return Value::makeNull();
  auto value = std::move((*array)[index]);
  array->erase(array->begin() + index);
  return value;
}

// Range helpers
bool VM::isInRange(RangeRef range_ref, int64_t value) {
  auto *r = heap_.range(range_ref.id);
  if (!r)
    return false;

  if (r->step > 0) {
    return value >= r->start && value < r->end &&
           (value - r->start) % r->step == 0;
  } else {
    return value <= r->start && value > r->end &&
           (r->start - value) % (-r->step) == 0;
  }
}

// Struct helpers
uint32_t VM::registerStructType(const std::string &name,
                                const std::vector<std::string> &fields) {
  return heap_.registerStructType(name, fields);
}

StructRef VM::createStruct(uint32_t typeId, size_t fieldCount) {
  return heap_.allocateStruct(typeId, fieldCount);
}

Value VM::getStructField(StructRef struct_ref, size_t index) {
  auto it = heap_.structs_.find(struct_ref.id);
  if (it == heap_.structs_.end() || index >= it->second.size()) {
    return Value::makeNull();
  }
  return it->second[index];
}

void VM::setStructField(StructRef struct_ref, size_t index,
                        const Value &value) {
  auto it = heap_.structs_.find(struct_ref.id);
  if (it == heap_.structs_.end() || index >= it->second.size()) {
    return;
  }
  it->second[index] = value;
}

uint32_t VM::getStructTypeId(StructRef struct_ref) { return struct_ref.typeId; }

// Class helpers
uint32_t VM::registerClassType(const std::string &name,
                               const std::vector<std::string> &fields,
                               uint32_t parentTypeId) {
  return heap_.registerClassType(name, fields, parentTypeId);
}

ClassRef VM::createClass(uint32_t typeId, size_t fieldCount,
                         uint32_t parentInstanceId) {
  return heap_.allocateClass(typeId, fieldCount, parentInstanceId);
}

uint32_t VM::getClassParentTypeId(uint32_t typeId) const {
  return heap_.getClassParentTypeId(typeId);
}

void VM::registerClassMethod(uint32_t typeId, const std::string &methodName,
                             uint32_t functionIndex) {
  heap_.registerClassMethod(typeId, methodName, functionIndex);
}

std::optional<uint32_t>
VM::findClassMethod(uint32_t typeId, const std::string &methodName) const {
  return heap_.findClassMethod(typeId, methodName);
}

Value VM::getClassField(ClassRef class_ref, size_t index) {
  auto it = heap_.classes_.find(class_ref.id);
  if (it == heap_.classes_.end() || index >= it->second.size()) {
    return Value::makeNull();
  }
  return it->second[index];
}

void VM::setClassField(ClassRef class_ref, size_t index,
                       const Value &value) {
  auto it = heap_.classes_.find(class_ref.id);
  if (it == heap_.classes_.end() || index >= it->second.size()) {
    return;
  }
  it->second[index] = value;
}

uint32_t VM::getClassTypeId(ClassRef class_ref) { return class_ref.typeId; }

// Copy a struct (value type semantics)
StructRef VM::copyStruct(StructRef struct_ref) {
  const size_t field_count = heap_.structFieldCount(struct_ref.typeId);
  StructRef copy = createStruct(struct_ref.typeId, field_count);

  // Copy all fields
  auto it = heap_.structs_.find(struct_ref.id);
  if (it != heap_.structs_.end()) {
    for (size_t i = 0; i < field_count && i < it->second.size(); ++i) {
      setStructField(copy, i, it->second[i]);
    }
  }

  return copy;
}

// Enum helpers
uint32_t VM::registerEnumType(const std::string &name,
                              const std::vector<std::string> &variants) {
  return heap_.registerEnumType(name, variants);
}

EnumRef VM::createEnum(uint32_t typeId, uint32_t tag, size_t payloadCount) {
  return heap_.allocateEnum(typeId, tag, payloadCount);
}

uint32_t VM::getEnumTag(EnumRef enum_ref) { return enum_ref.tag; }

Value VM::getEnumPayload(EnumRef enum_ref, size_t index) {
  auto it = heap_.enums_.find(enum_ref.id);
  if (it == heap_.enums_.end() || index >= it->second.second.size()) {
    return Value::makeNull();
  }
  return it->second.second[index];
}

void VM::setEnumPayload(EnumRef enum_ref, size_t index,
                        const Value &value) {
  auto it = heap_.enums_.find(enum_ref.id);
  if (it == heap_.enums_.end() || index >= it->second.second.size()) {
    return;
  }
  it->second.second[index] = value;
}

// Membership helpers
bool VM::arrayContains(ArrayRef array_ref, const Value &value) {
  auto *array = heap_.array(array_ref.id);
  if (!array)
    return false;
  for (const auto &item : *array) {
    if (valuesEqual(item, value)) {
      return true;
    }
  }
  return false;
}

bool VM::objectHasKey(ObjectRef object_ref, const std::string &key) {
  auto *object = heap_.object(object_ref.id);
  if (!object)
    return false;
  return object->find(key) != object->end();
}

// Iterator helpers
IteratorRef VM::createIterator(const Value &iterable) {
  IteratorRef ref;
  ref.id = heap_.createIterator(iterable);
  return ref;
}

Value VM::iteratorNext(IteratorRef iterRef) {
  return heap_.iteratorNext(iterRef.id);
}

// Object helpers
std::vector<std::string> VM::getHostObjectKeys(ObjectRef object_ref) {
  auto *object = heap_.object(object_ref.id);
  if (!object)
    return {};
  std::vector<std::string> keys;
  keys.reserve(object->size());
  for (const auto &[key, value] : *object) {
    keys.push_back(key);
  }
  return keys;
}

std::vector<std::pair<std::string, Value>>
VM::getHostObjectEntries(ObjectRef object_ref) {
  auto *object = heap_.object(object_ref.id);
  if (!object)
    return {};
  return std::vector<std::pair<std::string, Value>>(object->begin(),
                                                            object->end());
}

bool VM::hasHostObjectField(ObjectRef object_ref, const std::string &key) {
  auto *object = heap_.object(object_ref.id);
  if (!object)
    return false;
  return object->find(key) != object->end();
}

Value VM::getHostObjectField(ObjectRef object_ref,
                                     const std::string &key) {
  auto *object = heap_.object(object_ref.id);
  if (!object)
    return Value::makeNull();
  auto it = object->find(key);
  if (it == object->end())
    return Value::makeNull();
  return it->second;
}

bool VM::deleteHostObjectField(ObjectRef object_ref, const std::string &key) {
  auto *object = heap_.object(object_ref.id);
  if (!object)
    return false;
  return object->erase(key) > 0;
}

void VM::setHostObjectFrozen(ObjectRef, bool) {
  // TODO: Implement object freezing
}

void VM::setHostObjectSealed(ObjectRef, bool) {
  // TODO: Implement object sealing
}

// Function calling
Value VM::callHostFunction(const Value &fn,
                                   const std::vector<Value> &args) {
  if (fn.isHostFuncId()) {
    // TODO: host func name lookup
    // For now, return null since we can't resolve the name without a table
    (void)fn.asHostFuncId();
    return Value::makeNull();
  }
  return Value::makeNull();
}

// General function call (handles both VM closures and host functions)
Value VM::callFunction(const Value &fn,
                               const std::vector<Value> &args) {
  // Host function - direct call
  if (fn.isHostFuncId()) {
    return callHostFunction(fn, args);
  }

  // VM Closure or FunctionObject - use synchronous call with state isolation
  return callFunctionSync(fn, args);
}

// Prototype system - methods on types
void VM::registerPrototypeMethod(const std::string &typeName,
                                 const std::string &methodName,
                                 HostFunctionRef method) {
  prototypes_[typeName][methodName] = method;
}

std::optional<HostFunctionRef>
VM::getPrototypeMethod(const Value &value,
                       const std::string &methodName) {
  // Determine type name
  std::string typeName;
  if (value.isStringValId()) {
    typeName = "String";
  } else if (value.isArrayId()) {
    typeName = "Array";
  } else if (value.isObjectId()) {
    typeName = "Object";
  } else {
    return std::nullopt;
  }

  // Look up method in prototype
  auto typeIt = prototypes_.find(typeName);
  if (typeIt == prototypes_.end())
    return std::nullopt;

  auto methodIt = typeIt->second.find(methodName);
  if (methodIt == typeIt->second.end())
    return std::nullopt;

  return methodIt->second;
}

std::vector<std::string> VM::getPrototypeMethods(const Value &value) {
  std::string typeName;
  if (value.isStringValId()) {
    typeName = "String";
  } else if (value.isArrayId()) {
    typeName = "Array";
  } else if (value.isObjectId()) {
    typeName = "Object";
  } else {
    return {};
  }

  auto typeIt = prototypes_.find(typeName);
  if (typeIt == prototypes_.end())
    return {};

  std::vector<std::string> methods;
  for (const auto &[name, fn] : typeIt->second) {
    methods.push_back(name);
  }
  return methods;
}

uint64_t VM::pinExternalRoot(const Value &value) {
  return heap_.pinExternalRoot(value);
}

bool VM::unpinExternalRoot(uint64_t root_id) {
  return heap_.unpinExternalRoot(root_id);
}

std::optional<Value> VM::externalRootValue(uint64_t root_id) const {
  return heap_.externalRoot(root_id);
}

void VM::registerDefaultHostFunctions() {
  // Register print as both host function AND global (for closure access)
  registerHostFunction("print", [this](const std::vector<Value> &args) {
    // Check if last arg is kwargs object (has end= or delim=)
    std::string delim = " ";
    std::string end = "\n";
    size_t argCount = args.size();

    // Check for kwargs object as last argument
    bool hasKwargs = false;
    if (!args.empty() && args.back().isObjectId()) {
      auto *kwargsObj = heap_.object(args.back().asObjectId());
      if (kwargsObj) {
        auto itEnd = kwargsObj->find("end");
        bool foundEnd = itEnd != kwargsObj->end();
        auto itDelim = kwargsObj->find("delim");
        bool foundDelim = itDelim != kwargsObj->end();
        if (foundEnd) {
          end = resolveStringKey(itEnd->second);
        }
        if (foundDelim) {
          delim = resolveStringKey(itDelim->second);
        }
        // Only treat as kwargs if it has at least one of end/delim
        if (foundEnd || foundDelim) {
          hasKwargs = true;
        }
      }
    }
    if (hasKwargs) {
      argCount--; // Don't count kwargs as a value to print
    }

    // Print values with delimiter
    for (size_t i = 0; i < argCount; ++i) {
      if (i > 0) {
        std::cout << delim;
      }
      // For string values, resolve them; for other types use heap-aware toString
      const auto &arg = args[i];
      if (arg.isStringValId() || arg.isStringId()) {
        std::cout << resolveStringKey(arg);
      } else {
        std::string s = toString(arg);
        std::cout << s;
      }
    }
    std::cout << end;
    return Value::makeNull();
  });

  // fmt(format_string, ...) - Python-style string formatting
  registerHostFunction("fmt", [this](const std::vector<Value> &args) {
    if (args.empty()) {
      COMPILER_THROW("fmt() requires at least a format string");
    }

    // Get format string
    if (!args[0].isStringValId()) {
      COMPILER_THROW("fmt() format must be a string");
    }
    // TODO: string pool lookup
    std::string formatStr = "<string:" + std::to_string(args[0].asStringValId()) + ">";

    // Convert args to strings for formatting
    std::vector<std::string> argStrings;
    for (size_t i = 1; i < args.size(); ++i) {
      argStrings.push_back(toString(args[i]));
    }

    // Simple format string processing: {} placeholders
    std::string result;
    size_t argIndex = 0;
    size_t pos = 0;

    while (pos < formatStr.size()) {
      size_t placeholder = formatStr.find("{}", pos);
      if (placeholder == std::string::npos) {
        // No more placeholders, append rest of string
        result += formatStr.substr(pos);
        break;
      }

      // Append text before placeholder
      result += formatStr.substr(pos, placeholder - pos);

      // Replace placeholder with argument
      if (argIndex < argStrings.size()) {
        result += argStrings[argIndex++];
      } else {
        result += "{}"; // No more args, keep placeholder
      }

      pos = placeholder + 2; // Skip past {}
    }

    return Value::makeNull();
  });

  registerHostFunction("clock_ms", 0, [](const std::vector<Value> &) {
    const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now())
                         .time_since_epoch()
                         .count();
    return Value::makeInt(static_cast<int64_t>(now));
  });

  registerHostFunction(
      "sleep_ms", 1, [](const std::vector<Value> &args) {
        if (!args[0].isInt()) {
          COMPILER_THROW(
              "sleep_ms expects exactly 1 integer argument");
        }

        int64_t duration_ms = args[0].asInt();
        if (duration_ms < 0) {
          COMPILER_THROW("sleep_ms duration cannot be negative");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
        return Value::makeNull();
      });

  // Read a line from stdin
  registerHostFunction("input", [this](const std::vector<Value> &args) {
    // Optional prompt
    if (!args.empty()) {
      std::string prompt = resolveStringKey(args[0]);
      std::cout << prompt << std::flush;
    }
    std::string line;
    std::getline(std::cin, line);
    auto strRef = heap_.allocateString(line);
    return Value::makeStringId(strRef.id);
  });

  // Enhanced sleep() with duration string support
  registerHostFunction(
      "sleep", 1, [this](const std::vector<Value> &args) {
        if (args.empty()) {
          COMPILER_THROW("sleep() requires one argument");
        }

        auto duration_ms = parseDuration(args[0]);
        if (!duration_ms) {
          COMPILER_THROW(
              "sleep(): invalid duration format. Use numbers (ms) or strings "
              "like '1s', '500ms', '2.5m', '1h'");
        }

        if (*duration_ms < 0) {
          COMPILER_THROW("sleep(): duration cannot be negative");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(*duration_ms));
        return Value::makeNull();
      });

  // Type conversion builtins
  registerHostFunction("int", 1, [this](const std::vector<Value> &args) {
    return Value(toInt(args[0]));
  });

  registerHostFunction("num", 1, [this](const std::vector<Value> &args) {
    return Value(toFloat(args[0]));
  });

  // Instrumentation: assert(condition, message?)
  registerHostFunction("assert", [this](const std::vector<Value> &args) {
    if (args.empty()) {
      COMPILER_THROW(
          "assert() requires at least a condition argument");
    }
    bool condition = toBool(args[0]);
    if (!condition) {
      std::string msg = "Assertion failed";
      if (args.size() > 1 &&
          (args[1].isStringValId() || args[1].isStringId())) {
        msg = resolveStringKey(args[1]);
      }
      COMPILER_THROW(msg);
    }
    return Value::makeNull();
  });

  registerHostFunction("panic", [this](const std::vector<Value> &args) {
    std::string msg = "panic";
    if (!args.empty()) {
      msg = resolveStringKey(args[0]);
    }
    COMPILER_THROW(msg);
    return Value::makeNull();
  });

  // Performance: clock_ns() - high-resolution clock in nanoseconds
  registerHostFunction("clock_ns", 0, [](const std::vector<Value> &) {
    const auto now = std::chrono::time_point_cast<std::chrono::nanoseconds>(
                         std::chrono::steady_clock::now())
                         .time_since_epoch()
                         .count();
    return Value::makeInt(static_cast<int64_t>(now));
  });

  // Performance: clock_us() - clock in microseconds
  registerHostFunction("clock_us", 0, [](const std::vector<Value> &) {
    const auto now = std::chrono::time_point_cast<std::chrono::microseconds>(
                         std::chrono::steady_clock::now())
                         .time_since_epoch()
                         .count();
    return Value::makeInt(static_cast<int64_t>(now));
  });

  // str() builtin returns string representation - TODO: implement proper string pool creation
  registerHostFunction("str", 1, [this](const std::vector<Value> &args) {
    (void)this->toString(args[0]);
    return Value::makeNull();
  });

  // type() builtin returns type name
  registerHostFunction("type", 1, [this](const std::vector<Value> &args) {
    const auto &value = args[0];
    std::string typeName;
    if (value.isNull()) typeName = "null";
    else if (value.isBool()) typeName = "bool";
    else if (value.isInt()) typeName = "int";
    else if (value.isDouble()) typeName = "float";
    else if (value.isStringValId() || value.isStringId()) typeName = "string";
    else if (value.isArrayId()) typeName = "array";
    else if (value.isObjectId()) typeName = "object";
    else if (value.isRangeId()) typeName = "range";
    else if (value.isHostFuncId()) typeName = "function";
    else if (value.isClosureId()) typeName = "closure";
    else if (value.isFunctionObjId()) typeName = "function";
    else if (value.isEnumId()) typeName = "enum";
    else if (value.isIteratorId()) typeName = "iterator";
    else if (value.isStructId()) typeName = "struct";
    else if (value.isClassId()) typeName = "class";
    else typeName = "unknown";
    auto strRef = heap_.allocateString(typeName);
    return Value::makeStringId(strRef.id);
  });

  // Async library functions
  registerHostFunction("async.run", 1, [this](const std::vector<Value> &args) {
    if (args.empty()) {
      COMPILER_THROW("async.run requires a closure argument");
    }
    // Execute closure synchronously - full async isolation requires
    // additional infrastructure (closure serialization, thread pools)
    return this->call(args[0], {});
  });

  registerHostFunction("async.await", 1, [this](const std::vector<Value> &args) {
    if (args.empty()) {
      return Value::makeNull();
    }
    // For synchronous async.run, just return the value directly
    return args[0];
  });

  registerHostFunction("async.sleep", 1, [this](const std::vector<Value> &args) {
    if (args.empty() || !args[0].isNumber()) {
      COMPILER_THROW("async.sleep(ms) expects numeric argument");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(toInt(args[0])));
    return Value::makeNull();
  });

  // ========================================================================
  // Thread, Interval, and Timeout - Concurrency primitives
  // ========================================================================

  // thread { closure } - Create and start a message-passing thread
  registerHostFunction("thread", 1, [this](const std::vector<Value> &args) {
    if (args.empty() || (!args[0].isClosureId() && !args[0].isFunctionObjId())) {
      COMPILER_THROW("thread requires a closure argument");
    }
    
    auto threadObj = std::make_shared<Thread>();
    auto closure = args[0];
    
    // Create message handler that invokes the closure
    auto handler = [this, closure](const Thread::Message &msg) {
      try {
        // Convert message to Value and call closure
        Value arg;
        if (std::holds_alternative<std::string>(msg)) {
          auto strRef = heap_.allocateString(std::get<std::string>(msg));
          arg = Value::makeStringId(strRef.id);
        } else if (std::holds_alternative<int>(msg)) {
          arg = Value::makeInt(std::get<int>(msg));
        } else if (std::holds_alternative<double>(msg)) {
          arg = Value::makeDouble(std::get<double>(msg));
        }
        
        this->call(closure, {arg});
      } catch (const std::exception &e) {
        std::cerr << "[thread] Exception: " << e.what() << std::endl;
      }
    };
    
    threadObj->start(std::move(handler));
    
    // Store thread in GC heap and return wrapper object
    auto threadRef = heap_.allocateThreadObj(threadObj);
    return Value::makeThreadId(threadRef.id);
  });

  // thread.send(thread, message) - Send message to thread
  registerHostFunction("thread.send", 2, [this](const std::vector<Value> &args) {
    if (args.size() < 2 || !args[0].isThreadId()) {
      COMPILER_THROW("thread.send requires a thread object and message");
    }
    
    auto *threadObj = heap_.thread(args[0].asThreadId());
    if (!threadObj) {
      COMPILER_THROW("thread.send: invalid thread reference");
    }
    
    // Convert message Value to Thread::Message
    Thread::Message msg;
    if (args[1].isStringValId()) {
      auto *str = heap_.string(args[1].asStringValId());
      if (str) {
        msg = *str;
      } else {
        COMPILER_THROW("thread.send: invalid string reference");
      }
    } else if (args[1].isInt()) {
      msg = static_cast<int>(args[1].asInt());
    } else if (args[1].isDouble() || args[1].isNumber()) {
      msg = args[1].isDouble() ? args[1].asDouble() : static_cast<double>(args[1].asInt());
    } else {
      COMPILER_THROW("thread.send: message must be string, int, or number");
    }
    
    threadObj->send(msg);
    return Value::makeNull();
  });

  // thread.pause(thread) - Pause thread
  registerHostFunction("thread.pause", 1, [this](const std::vector<Value> &args) {
    if (args.empty() || !args[0].isThreadId()) {
      COMPILER_THROW("thread.pause requires a thread argument");
    }
    
    auto *threadObj = heap_.thread(args[0].asThreadId());
    if (!threadObj) {
      COMPILER_THROW("thread.pause: invalid thread reference");
    }
    
    threadObj->pause();
    return Value::makeNull();
  });

  // thread.resume(thread) - Resume thread
  registerHostFunction("thread.resume", 1, [this](const std::vector<Value> &args) {
    if (args.empty() || !args[0].isThreadId()) {
      COMPILER_THROW("thread.resume requires a thread argument");
    }
    
    auto *threadObj = heap_.thread(args[0].asThreadId());
    if (!threadObj) {
      COMPILER_THROW("thread.resume: invalid thread reference");
    }
    
    threadObj->resume();
    return Value::makeNull();
  });

  // thread.stop(thread) - Stop thread
  registerHostFunction("thread.stop", 1, [this](const std::vector<Value> &args) {
    if (args.empty() || !args[0].isThreadId()) {
      COMPILER_THROW("thread.stop requires a thread argument");
    }
    
    auto *threadObj = heap_.thread(args[0].asThreadId());
    if (!threadObj) {
      COMPILER_THROW("thread.stop: invalid thread reference");
    }
    
    threadObj->stop();
    return Value::makeNull();
  });

  // thread.running(thread) -> bool - Check if thread is running
  registerHostFunction("thread.running", 1, [this](const std::vector<Value> &args) {
    if (args.empty() || !args[0].isThreadId()) {
      COMPILER_THROW("thread.running requires a thread argument");
    }
    
    auto *threadObj = heap_.thread(args[0].asThreadId());
    if (!threadObj) {
      return Value::makeBool(false);
    }
    
    return Value::makeBool(threadObj->isRunning());
  });

  // interval(ms, closure) - Create repeating timer
  registerHostFunction("interval", 2, [this](const std::vector<Value> &args) {
    if (args.size() < 2 || !args[0].isNumber()) {
      COMPILER_THROW("interval requires milliseconds and closure");
    }
    if (!args[1].isClosureId() && !args[1].isFunctionObjId()) {
      COMPILER_THROW("interval requires a closure argument");
    }
    
    int ms = toInt(args[0]);
    auto closure = args[1];
    
    auto callback = [this, closure]() {
      try {
        this->call(closure, {});
      } catch (const std::exception &e) {
        std::cerr << "[interval] Exception: " << e.what() << std::endl;
      }
    };
    
    auto intervalObj = std::make_shared<Interval>(ms, std::move(callback));
    auto intervalRef = heap_.allocateIntervalObj(intervalObj);
    return Value::makeIntervalId(intervalRef.id);
  });

  // interval.pause(interval) - Pause interval
  registerHostFunction("interval.pause", 1, [this](const std::vector<Value> &args) {
    if (args.empty() || !args[0].isIntervalId()) {
      COMPILER_THROW("interval.pause requires an interval argument");
    }
    
    auto *intervalObj = heap_.interval(args[0].asIntervalId());
    if (!intervalObj) {
      COMPILER_THROW("interval.pause: invalid interval reference");
    }
    
    intervalObj->pause();
    return Value::makeNull();
  });

  // interval.resume(interval) - Resume interval
  registerHostFunction("interval.resume", 1, [this](const std::vector<Value> &args) {
    if (args.empty() || !args[0].isIntervalId()) {
      COMPILER_THROW("interval.resume requires an interval argument");
    }
    
    auto *intervalObj = heap_.interval(args[0].asIntervalId());
    if (!intervalObj) {
      COMPILER_THROW("interval.resume: invalid interval reference");
    }
    
    intervalObj->resume();
    return Value::makeNull();
  });

  // interval.stop(interval) - Stop interval
  registerHostFunction("interval.stop", 1, [this](const std::vector<Value> &args) {
    if (args.empty() || !args[0].isIntervalId()) {
      COMPILER_THROW("interval.stop requires an interval argument");
    }
    
    auto *intervalObj = heap_.interval(args[0].asIntervalId());
    if (!intervalObj) {
      COMPILER_THROW("interval.stop: invalid interval reference");
    }
    
    intervalObj->stop();
    return Value::makeNull();
  });

  // timeout(ms, closure) - Create one-shot delayed execution
  registerHostFunction("timeout", 2, [this](const std::vector<Value> &args) {
    if (args.size() < 2 || !args[0].isNumber()) {
      COMPILER_THROW("timeout requires milliseconds and closure");
    }
    if (!args[1].isClosureId() && !args[1].isFunctionObjId()) {
      COMPILER_THROW("timeout requires a closure argument");
    }
    
    int ms = toInt(args[0]);
    auto closure = args[1];
    
    auto callback = [this, closure]() {
      try {
        this->call(closure, {});
      } catch (const std::exception &e) {
        std::cerr << "[timeout] Exception: " << e.what() << std::endl;
      }
    };
    
    auto timeoutObj = std::make_shared<Timeout>(ms, std::move(callback));
    auto timeoutRef = heap_.allocateTimeoutObj(timeoutObj);
    return Value::makeTimeoutId(timeoutRef.id);
  });

  // timeout.cancel(timeout) - Cancel timeout
  registerHostFunction("timeout.cancel", 1, [this](const std::vector<Value> &args) {
    if (args.empty() || !args[0].isTimeoutId()) {
      COMPILER_THROW("timeout.cancel requires a timeout argument");
    }
    
    auto *timeoutObj = heap_.timeout(args[0].asTimeoutId());
    if (!timeoutObj) {
      COMPILER_THROW("timeout.cancel: invalid timeout reference");
    }
    
    timeoutObj->cancel();
    return Value::makeNull();
  });

  // GC control
  auto registerSystemGc = [this](const std::string &name) {
    registerHostFunction(name, 0, [this](const std::vector<Value> &) {
      runGarbageCollection();
      return Value::makeNull();
    });
  };
  registerSystemGc("system.gc");
  registerSystemGc("system_gc");

  auto registerSystemGcStats = [this](const std::string &name) {
    registerHostFunction(name, 0, [this](const std::vector<Value> &) {
      const auto stats = gcStats();
      const auto object_ref = createHostObject();
      setHostObjectField(object_ref, "heapSize",
                         Value::makeInt(static_cast<int64_t>(stats.heap_size)));
      setHostObjectField(object_ref, "objectCount",
                         Value::makeInt(static_cast<int64_t>(stats.object_count)));
      setHostObjectField(object_ref, "collections",
                         Value::makeInt(static_cast<int64_t>(stats.collections)));
      setHostObjectField(object_ref, "lastPauseNs",
                         Value::makeInt(static_cast<int64_t>(stats.last_pause_ns)));
      return Value::makeObjectId(object_ref.id);
    });
  };
  registerSystemGcStats("system.gcStats");
  registerSystemGcStats("system_gcStats");

  // Struct operations (value type)
  registerHostFunction(
      "struct.define", 2, [this](const std::vector<Value> &args) {
        if (args.size() != 2 || !args[0].isStringValId() ||
            !args[1].isArrayId()) {
          COMPILER_THROW(
              "struct.define(name, fields) expects (string, array)");
        }
        if (!current_chunk) COMPILER_THROW("struct.define requires active chunk");

        const auto &name = current_chunk->getString(args[0].asStringValId());
        auto *field_array = heap_.array(args[1].asArrayId());
        if (!field_array) {
          COMPILER_THROW("struct.define received invalid fields array");
        }

        std::vector<std::string> fields;
        fields.reserve(field_array->size());
        for (const auto &value : *field_array) {
          if (!value.isStringValId()) {
            COMPILER_THROW("struct.define fields must contain only strings");
          }
          fields.push_back(current_chunk->getString(value.asStringValId()));
        }

        uint32_t type_id = registerStructType(name, fields);
        struct_type_ids_by_name_[name] = type_id;
        return Value::makeInt(static_cast<int64_t>(type_id));
      });

  registerHostFunction(
      "struct.new", [this](const std::vector<Value> &args) {
        if (args.empty()) {
          COMPILER_THROW(
              "struct.new(type, ...values) requires a type argument");
        }
        uint32_t type_id = 0;
        if (args[0].isInt()) {
          type_id = static_cast<uint32_t>(args[0].asInt());
        } else if (args[0].isStringValId()) {
          if (!current_chunk) COMPILER_THROW("struct.new requires active chunk");
          const auto &name = current_chunk->getString(args[0].asStringValId());
          auto it = struct_type_ids_by_name_.find(name);
          if (it == struct_type_ids_by_name_.end()) {
            COMPILER_THROW("Unknown struct type: " + name);
          }
          type_id = it->second;
        } else {
          COMPILER_THROW("struct.new type must be string or int");
        }

        const size_t field_count = heap_.structFieldCount(type_id);
        StructRef ref = createStruct(type_id, field_count);
        struct_instance_type_ids_[ref.id] = type_id;
        const size_t provided = args.size() - 1;
        for (size_t i = 0; i < provided && i < field_count; ++i) {
          setStructField(ref, i, args[i + 1]);
        }
        return Value::makeStructId(ref.id, type_id);
      });

  registerHostFunction(
      "struct.get", [this](const std::vector<Value> &args) {
        if (args.size() != 2 || !args[0].isStructId()) {
          COMPILER_THROW("struct.get(struct, field) expects struct");
        }
        StructRef ref{args[0].asStructId(), args[0].asStructTypeId()};
        size_t index = 0;
        if (args[1].isInt()) {
          index = static_cast<size_t>(args[1].asInt());
        } else if (args[1].isStringValId()) {
          const std::string& fieldName = current_chunk->getString(args[1].asStringValId());
          auto idx = heap_.structFieldIndex(ref.typeId, fieldName);
          if (!idx.has_value()) {
            return Value::makeNull();
          }
          index = *idx;
        } else {
          COMPILER_THROW("struct.get field must be string or int");
        }
        return getStructField(ref, index);
      });

  registerHostFunction(
      "struct.set", [this](const std::vector<Value> &args) {
        if (args.size() != 3 || !args[0].isStructId()) {
          COMPILER_THROW(
              "struct.set(struct, field, value) expects struct");
        }
        StructRef ref{args[0].asStructId(), args[0].asStructTypeId()};
        size_t index = 0;
        if (args[1].isInt()) {
          index = static_cast<size_t>(args[1].asInt());
        } else if (args[1].isStringValId()) {
          const std::string& fieldName = current_chunk->getString(args[1].asStringValId());
          auto idx = heap_.structFieldIndex(ref.typeId, fieldName);
          if (!idx.has_value()) {
            COMPILER_THROW("Unknown struct field name: " + fieldName);
          }
          index = *idx;
        } else {
          COMPILER_THROW("struct.set field must be string or int");
        }
        setStructField(ref, index, args[2]);
        return Value::makeStructId(ref.id, ref.typeId);
      });

  // Class operations (reference type)
  registerHostFunction(
      "class.define", [this](const std::vector<Value> &args) {
        if ((args.size() != 2 && args.size() != 3) || !args[0].isStringValId() ||
            !args[1].isArrayId()) {
          COMPILER_THROW(
              "class.define(name, fields, parent?) expects (string, array, optional class)");
        }
        if (!current_chunk) COMPILER_THROW("class.define requires active chunk");

        const auto &name = current_chunk->getString(args[0].asStringValId());
        auto *field_array = heap_.array(args[1].asArrayId());
        if (!field_array) {
          COMPILER_THROW("class.define received invalid fields array");
        }

        std::vector<std::string> fields;
        fields.reserve(field_array->size());
        for (const auto &value : *field_array) {
          if (!value.isStringValId()) {
            COMPILER_THROW("class.define fields must contain only strings");
          }
          fields.push_back(current_chunk->getString(value.asStringValId()));
        }

        uint32_t parent_type_id = 0;
        if (args.size() == 3) {
          if (args[2].isInt()) {
            parent_type_id = static_cast<uint32_t>(args[2].asInt());
          } else if (args[2].isStringValId()) {
            const auto &parent_name = current_chunk->getString(args[2].asStringValId());
            auto parent_it = class_type_ids_by_name_.find(parent_name);
            if (parent_it == class_type_ids_by_name_.end()) {
              COMPILER_THROW("Unknown parent class type: " + parent_name);
            }
            parent_type_id = parent_it->second;
          } else {
            COMPILER_THROW("class.define parent must be class type id or class name");
          }
        }

        uint32_t type_id = registerClassType(name, fields, parent_type_id);
        class_type_ids_by_name_[name] = type_id;
        return Value::makeInt(static_cast<int64_t>(type_id));
      });

  registerHostFunction(
      "class.new", [this](const std::vector<Value> &args) {
        if (args.empty()) {
          COMPILER_THROW(
              "class.new(type, ...values) requires a type argument");
        }
        uint32_t type_id = 0;
        if (args[0].isInt()) {
          type_id = static_cast<uint32_t>(args[0].asInt());
        } else if (args[0].isStringValId()) {
          if (!current_chunk) COMPILER_THROW("class.new requires active chunk");
          const auto &name = current_chunk->getString(args[0].asStringValId());
          auto it = class_type_ids_by_name_.find(name);
          if (it == class_type_ids_by_name_.end()) {
            COMPILER_THROW("Unknown class type: " + name);
          }
          type_id = it->second;
        } else {
          COMPILER_THROW("class.new type must be string or int");
        }

        const size_t field_count = heap_.classFieldCount(type_id);
        ClassRef ref = createClass(type_id, field_count);
        class_instance_type_ids_[ref.id] = type_id;
        auto init_method = findClassMethod(type_id, "init");
        if (init_method.has_value()) {
          std::vector<Value> ctor_args;
          ctor_args.reserve(args.size());
          ctor_args.push_back(Value::makeClassId(ref.id, type_id));
          for (size_t i = 1; i < args.size(); ++i) {
            ctor_args.push_back(args[i]);
          }
          (void)call(Value::makeFunctionObjId(*init_method), ctor_args);
        } else {
          const size_t provided = args.size() - 1;
          for (size_t i = 0; i < provided && i < field_count; ++i) {
            setClassField(ref, i, args[i + 1]);
          }
        }
        return Value::makeClassId(ref.id, type_id);
      });

  registerHostFunction(
      "class.method", [this](const std::vector<Value> &args) {
        if (args.size() != 3 || !args[0].isInt() || !args[1].isStringValId() ||
            !args[2].isFunctionObjId()) {
          COMPILER_THROW("class.method(type_id, name, function) expects (int, string, function)");
        }
        if (!current_chunk) COMPILER_THROW("class.method requires active chunk");
        const uint32_t type_id = static_cast<uint32_t>(args[0].asInt());
        const std::string &method_name = current_chunk->getString(args[1].asStringValId());
        registerClassMethod(type_id, method_name, args[2].asFunctionObjId());
        return Value::makeNull();
      });

  registerHostFunction(
      "class.get", [this](const std::vector<Value> &args) {
        if (args.size() != 2 || !args[0].isClassId()) {
          COMPILER_THROW("class.get(class, field) expects class");
        }
        uint32_t ref_type_id = args[0].asClassTypeId();
        if (ref_type_id == 0) {
          auto it = class_instance_type_ids_.find(args[0].asClassId());
          if (it != class_instance_type_ids_.end()) {
            ref_type_id = it->second;
          }
        }
        ClassRef ref{args[0].asClassId(), ref_type_id};
        size_t index = 0;
        if (args[1].isInt()) {
          index = static_cast<size_t>(args[1].asInt());
        } else if (args[1].isStringValId()) {
          if (!current_chunk) COMPILER_THROW("class.get requires active chunk");
          const std::string& fieldName = current_chunk->getString(args[1].asStringValId());
          auto idx = heap_.classFieldIndex(ref.typeId, fieldName);
          if (!idx.has_value()) {
            return Value::makeNull();
          }
          index = *idx;
        } else {
          COMPILER_THROW("class.get field must be string or int");
        }
        return getClassField(ref, index);
      });

  registerHostFunction(
      "class.set", [this](const std::vector<Value> &args) {
        if (args.size() != 3 || !args[0].isClassId()) {
          COMPILER_THROW(
              "class.set(class, field, value) expects class");
        }
        uint32_t ref_type_id = args[0].asClassTypeId();
        if (ref_type_id == 0) {
          auto it = class_instance_type_ids_.find(args[0].asClassId());
          if (it != class_instance_type_ids_.end()) {
            ref_type_id = it->second;
          }
        }
        ClassRef ref{args[0].asClassId(), ref_type_id};
        size_t index = 0;
        if (args[1].isInt()) {
          index = static_cast<size_t>(args[1].asInt());
        } else if (args[1].isStringValId()) {
          if (!current_chunk) COMPILER_THROW("class.set requires active chunk");
          const std::string& fieldName = current_chunk->getString(args[1].asStringValId());
          auto idx = heap_.classFieldIndex(ref.typeId, fieldName);
          if (!idx.has_value()) {
            COMPILER_THROW("Unknown class field name: " + fieldName);
          }
          index = *idx;
        } else {
          COMPILER_THROW("class.set field must be string or int");
        }
        setClassField(ref, index, args[2]);
        return Value::makeClassId(ref.id, ref.typeId);
      });

  registerHostFunction(
      "inherits", [this](const std::vector<Value> &args) {
        if (args.size() != 2) {
          COMPILER_THROW("inherits(child, parent) expects two arguments");
        }
        auto read_type_id = [](const Value &v) -> uint32_t {
          if (v.isInt()) {
            return static_cast<uint32_t>(v.asInt());
          }
          if (v.isClassId()) {
            return v.asClassTypeId();
          }
          return 0;
        };

        uint32_t child = read_type_id(args[0]);
        uint32_t parent = read_type_id(args[1]);
        if (child == 0 || parent == 0) {
          return Value::makeBool(false);
        }
        uint32_t cursor = child;
        while (cursor != 0) {
          if (cursor == parent) {
            return Value::makeBool(true);
          }
          cursor = getClassParentTypeId(cursor);
        }
        return Value::makeBool(false);
      });
}

void VM::registerDefaultHostGlobals() {
  auto system_obj = heap_.allocateObject();
  setHostObjectField(system_obj, "gc", Value::makeHostFuncId(getHostFunctionIndex("system.gc")));
  setHostObjectField(system_obj, "gcStats", Value::makeHostFuncId(getHostFunctionIndex("system.gcStats")));
  setGlobal("system", Value::makeObjectId(system_obj.id));

  auto struct_obj = heap_.allocateObject();
  setHostObjectField(struct_obj, "define", Value::makeHostFuncId(getHostFunctionIndex("struct.define")));
  setHostObjectField(struct_obj, "new", Value::makeHostFuncId(getHostFunctionIndex("struct.new")));
  setHostObjectField(struct_obj, "get", Value::makeHostFuncId(getHostFunctionIndex("struct.get")));
  setHostObjectField(struct_obj, "set", Value::makeHostFuncId(getHostFunctionIndex("struct.set")));
  setGlobal("struct", Value::makeObjectId(struct_obj.id));

  auto class_obj = heap_.allocateObject();
  setHostObjectField(class_obj, "define", Value::makeHostFuncId(getHostFunctionIndex("class.define")));
  setHostObjectField(class_obj, "new", Value::makeHostFuncId(getHostFunctionIndex("class.new")));
  setHostObjectField(class_obj, "method", Value::makeHostFuncId(getHostFunctionIndex("class.method")));
  setHostObjectField(class_obj, "get", Value::makeHostFuncId(getHostFunctionIndex("class.get")));
  setHostObjectField(class_obj, "set", Value::makeHostFuncId(getHostFunctionIndex("class.set")));
  setGlobal("class", Value::makeObjectId(class_obj.id));

  auto async_obj = heap_.allocateObject();
  setHostObjectField(async_obj, "run", Value::makeHostFuncId(getHostFunctionIndex("async.run")));
  setHostObjectField(async_obj, "await", Value::makeHostFuncId(getHostFunctionIndex("async.await")));
  setHostObjectField(async_obj, "sleep", Value::makeHostFuncId(getHostFunctionIndex("async.sleep")));
  setGlobal("async", Value::makeObjectId(async_obj.id));

  // Process object - registered via system_object_initializer since
  // process.* functions are registered after default host globals
  // Will be set up in system_object_initializer if available

  // Register default window globals
  setGlobal("title", Value::makeNull());
  setGlobal("exe", Value::makeNull());
  setGlobal("pid", Value::makeInt(0));

  if (system_object_initializer_) {
    system_object_initializer_(this);
  }
}

Value VM::invokeHostFunction(const std::string &name,
                                     uint32_t arg_count) {
  auto it = host_functions.find(name);
  if (it == host_functions.end()) {
    COMPILER_THROW("Host function not found: " + name);
  }


  std::vector<Value> args(arg_count);
  for (uint32_t i = 0; i < arg_count; ++i) {
    if (stack.empty()) {
      COMPILER_THROW("Stack underflow while reading host arguments");
    }
    args[arg_count - 1 - i] = stack.top();
    stack.pop();
    // Debug: check what type the argument is
    std::string typeInfo = "unknown";
    if (args[arg_count - 1 - i].isNull()) typeInfo = "null";
    else if (args[arg_count - 1 - i].isInt()) typeInfo = "int";
    else if (args[arg_count - 1 - i].isClosureId()) typeInfo = "closure_id";
    else if (args[arg_count - 1 - i].isFunctionObjId()) typeInfo = "function_obj_id";
    else if (args[arg_count - 1 - i].isObjectId()) typeInfo = "object_id";
    else if (args[arg_count - 1 - i].isHostFuncId()) typeInfo = "host_func_id";
  }

  return it->second(args);
}

Value VM::invokeHostFunctionDirect(const std::string &name,
                                    const std::vector<Value> &args) {
  auto it = host_functions.find(name);
  if (it == host_functions.end()) {
    return Value::makeNull();
  }
  return it->second(args);
}

Value VM::execute(const BytecodeChunk &chunk,
                          const std::string &function_name,
                          const std::vector<Value> &args) {
  current_chunk = &chunk;

  const auto *entry = chunk.getFunction(function_name);
  if (!entry) {
    COMPILER_THROW("Function not found: " + function_name);
  }

  while (!stack.empty()) {
    stack.pop();
  }
  locals.clear();
  frame_count_ = 0;
  heap_.reset();
  struct_type_ids_by_name_.clear();
  class_type_ids_by_name_.clear();
  struct_instance_type_ids_.clear();
  class_instance_type_ids_.clear();
  open_upvalues.clear();
  has_current_exception_ = false;
  current_exception_ = nullptr;
  registerDefaultHostGlobals();
  opcode_counts_.fill(0);
  executed_instructions_ = 0;

  if (frame_arena_.size() <= frame_count_) {
    frame_arena_.push_back(CallFrame{entry, 0, 0, 0});
  } else {
    frame_arena_[frame_count_] = CallFrame{entry, 0, 0, 0};
  }
  frame_count_++;
  locals.resize(entry->local_count);

  if (!args.empty()) {
    if (args.size() != entry->param_count) {
      COMPILER_THROW("Argument count mismatch for entry function '" +
                               function_name + "' (expected " +
                               std::to_string(entry->param_count) + ", got " +
                               std::to_string(args.size()) + ")");
    }

    for (uint32_t i = 0; i < entry->param_count; ++i) {
      locals[i] = args[i];
    }
  }

  if (debug_mode) {
    std::cout << "=== Executing function: " << function_name
              << " ===" << std::endl;
  }

  runDispatchLoop(0);

  if (stack.empty()) {
    return nullptr;
  }

  Value result = stack.top();
  stack.pop();
  return result;
}

Value VM::executePersistent(const BytecodeChunk &chunk,
                                    const std::string &function_name,
                                    const std::vector<Value> &args) {
  current_chunk = &chunk;

  const auto *entry = chunk.getFunction(function_name);
  if (!entry) {
    COMPILER_THROW("Function not found: " + function_name);
  }

  // Clear stack and locals for this execution, but PRESERVE:
  // - globals (user-defined variables persist)
  // - heap (objects allocated by user persist)
  // - struct_type_ids (type information persists)
  while (!stack.empty()) {
    stack.pop();
  }
  locals.clear();
  frame_count_ = 0;
  // DON'T reset heap - preserves user globals
  // DON'T call registerDefaultHostGlobals - already registered
  open_upvalues.clear();
  has_current_exception_ = false;
  current_exception_ = nullptr;

  if (frame_arena_.size() <= frame_count_) {
    frame_arena_.push_back(CallFrame{entry, 0, 0, 0});
  } else {
    frame_arena_[frame_count_] = CallFrame{entry, 0, 0, 0};
  }
  frame_count_++;
  locals.resize(entry->local_count);

  if (!args.empty()) {
    if (args.size() != entry->param_count) {
      COMPILER_THROW("Argument count mismatch for entry function '" +
                               function_name + "' (expected " +
                               std::to_string(entry->param_count) + ", got " +
                               std::to_string(args.size()) + ")");
    }

    for (uint32_t i = 0; i < entry->param_count; ++i) {
      locals[i] = args[i];
    }
  }

  runDispatchLoop(0);

  if (stack.empty()) {
    return nullptr;
  }

  Value result = stack.top();
  stack.pop();
  return result;
}

void VM::runDispatchLoop(size_t stop_frame_depth) {
  while (frame_count_ > stop_frame_depth) {
    // CRITICAL: Capture ALL frame data by value BEFORE any mutation!
    // doCall() may cause vector reallocation, invalidating all
    // references/indices.
    size_t active_frame_idx = frame_count_ - 1;

    // Capture frame data by value - do NOT keep references!
    const auto *function = frame_arena_[active_frame_idx].function;
    uint32_t ip = frame_arena_[active_frame_idx].ip;
    size_t entry_frame_count = frame_count_;

    if (ip >= function->instructions.size()) {
      stack.push(nullptr);
      executeInstruction(Instruction{OpCode::RETURN});
      continue;
    }

    const auto &instruction = function->instructions[ip];


    if (debug_mode) {
      std::cout << "IP: " << ip
                << " OP: " << static_cast<int>(instruction.opcode) << std::endl;
    }

    try {
      if (profiling_enabled_) {
        opcode_counts_[static_cast<uint8_t>(instruction.opcode)]++;
        executed_instructions_++;
      }
      executeInstruction(instruction);
    } catch (const ScriptThrow &thrown) {
      if (!handleScriptThrow(thrown.value)) {
        // Build stack trace for uncaught exception
        std::string stackTrace = buildStackTrace(frame_count_);

        // Get line number from current instruction
        uint32_t line = 0;
        uint32_t column = 0;
        if (frame_count_ > 0) {
          auto &frame = frame_arena_[frame_count_ - 1];
          if (frame.function &&
              frame.ip < frame.function->instruction_locations.size()) {
            const auto loc = nearestSourceLocation(*frame.function, frame.ip);
            line = loc.line;
            column = loc.column;
          }
        }

        std::string errorMsg =
            "Uncaught exception: " + toString(thrown.value);
        if (line > 0) {
          errorMsg += " at line " + std::to_string(line);
          if (column > 0) {
            errorMsg += ":" + std::to_string(column);
          }
        }

        throw ScriptError(thrown.value, errorMsg, stackTrace, line, column);
      }
      continue;
    } catch (const std::runtime_error &e) {
      // Enrich runtime errors with source location
      std::string msg = e.what();
      if (frame_count_ > 0) {
        auto &frame = frame_arena_[frame_count_ - 1];
        if (frame.function &&
            frame.ip < frame.function->instruction_locations.size()) {
          const auto loc = nearestSourceLocation(*frame.function, frame.ip);
          if (loc.line > 0) {
            msg += " at " + std::to_string(loc.line) + ":" + std::to_string(loc.column);
          }
        }
      }
      throw std::runtime_error(msg);
    }

    processPendingCalls();

    // CRITICAL: Re-fetch frame AFTER executeInstruction (vector may have
    // reallocated). Only increment IP if the frame count didn't change
    // (no CALL/RETURN) and the instruction didn't modify IP itself.
    if (frame_count_ > stop_frame_depth) {
      active_frame_idx = frame_count_ - 1;
      if (frame_count_ == entry_frame_count &&
          frame_arena_[active_frame_idx].ip == ip) {
        frame_arena_[active_frame_idx].ip++;
      }
    }
  }
}

bool VM::handleScriptThrow(const Value &value) {
  has_current_exception_ = true;
  current_exception_ = value;

  while (frame_count_ > 0) {
    auto &frame = frame_arena_[frame_count_ - 1];
    if (!frame.try_stack.empty()) {
      const auto handler = frame.try_stack.back();
      frame.try_stack.pop_back();

      while (stack.size() > handler.stack_depth) {
        stack.pop();
      }

      // Jump to catch block (finally is compiled into the catch block if it
      // exists)
      frame.ip = handler.catch_ip;
      return true;
    }

    auto finished = frame;
    frame_count_--;

    closeFrameUpvalues(static_cast<uint32_t>(finished.locals_base),
                       static_cast<uint32_t>(locals.size()));
    if (locals.size() >= finished.locals_base) {
      locals.resize(finished.locals_base);
    }
  }

  // No handler found - exception is uncaught
  return false;
}

std::string VM::buildStackTrace(size_t frame_count) const {
  std::string trace;
  if (frame_count == 0) {
    return trace;
  }

  trace = "Stack trace:\n";
  for (size_t i = 0; i < frame_count; ++i) {
    const auto &frame = frame_arena_[i];
    if (!frame.function) {
      continue;
    }

    // Get function name if available
    std::string funcName = "<anonymous>";
    if (!frame.function->name.empty()) {
      funcName = frame.function->name;
    }

    // Get line/column from instruction location
    uint32_t line = 0;
    uint32_t column = 0;
    if (frame.ip < frame.function->instruction_locations.size()) {
      const auto &loc = frame.function->instruction_locations[frame.ip];
      line = loc.line;
      column = loc.column;
    }

    trace += "  at " + funcName;
    if (line > 0) {
      trace += " (line " + std::to_string(line);
      if (column > 0) {
        trace += ":" + std::to_string(column);
      }
      trace += ")";
    }
    trace += "\n";
  }

  return trace;
}

Value VM::call(const Value &callee_value,
                       const std::vector<Value> &args) {
  if (!current_chunk) {
    COMPILER_THROW(
        "VM::call requires an active bytecode chunk (run execute first)");
  }

  // Debug: check what type we received
  std::string typeInfo = "unknown";
  if (callee_value.isNull()) typeInfo = "null";
  else if (callee_value.isInt()) typeInfo = "int";
  else if (callee_value.isClosureId()) typeInfo = "closure_id";
  else if (callee_value.isFunctionObjId()) typeInfo = "function_obj_id";
  else if (callee_value.isObjectId()) typeInfo = "object_id";
  else if (callee_value.isHostFuncId()) typeInfo = "host_func_id";

  const size_t base_depth = frame_count_;
  doCall(callee_value, args, false);
  runDispatchLoop(base_depth);

  if (stack.empty()) {
    return nullptr;
  }
  Value result = stack.top();
  stack.pop();
  return result;
}

void VM::setDebugMode(bool enabled) { debug_mode = enabled; }

void VM::doCall(Value callee_value, std::vector<Value> args,
                bool advance_caller_ip) {

  // Handle host function call directly
  if (callee_value.isHostFuncId()) {
    uint32_t host_func_idx = callee_value.asHostFuncId();
    if (host_func_idx >= host_function_names_.size()) {
      COMPILER_THROW("Host function index out of range: " +
                               std::to_string(host_func_idx));
    }
    const std::string &name = host_function_names_[host_func_idx];
    auto it = host_functions.find(name);
    if (it == host_functions.end()) {
      COMPILER_THROW("Host function not found: " + name);
    }
    Value result = it->second(args); // Call and get result
    pushStack(result); // Push result to stack
    return;
  }

  if (frame_count_ >= max_call_depth_) {
    COMPILER_THROW("Stack overflow: maximum call depth " +
                             std::to_string(max_call_depth_) + " reached");
  }

  // Handle host function call (duplicate check after depth check)
  if (callee_value.isHostFuncId()) {
    // TODO: host func name lookup
    (void)callee_value.asHostFuncId();
    COMPILER_THROW("Host function call via doCall not yet supported with NaN boxing");
  }

  uint32_t function_index = 0;
  uint32_t closure_id = 0;
  if (callee_value.isFunctionObjId()) {
    function_index = callee_value.asFunctionObjId();
  } else if (callee_value.isClosureId()) {
    closure_id = callee_value.asClosureId();
    auto *closure = heap_.closure(closure_id);
    if (!closure) {
      COMPILER_THROW("Closure not found: " +
                               std::to_string(closure_id));
    }
    function_index = closure->function_index;
  } else {
    // Debug: identify what type the value actually is
    std::string typeInfo = "unknown";
    if (callee_value.isNull()) typeInfo = "null";
    else if (callee_value.isInt()) typeInfo = "int";
    else if (callee_value.isDouble()) typeInfo = "double";
    else if (callee_value.isBool()) typeInfo = "bool";
    else if (callee_value.isStringValId()) typeInfo = "string_val_id";
    else if (callee_value.isStringId()) typeInfo = "string_id";
    else if (callee_value.isObjectId()) typeInfo = "object_id";
    else if (callee_value.isArrayId()) typeInfo = "array_id";
    else if (callee_value.isHostFuncId()) typeInfo = "host_func_id";
    else if (callee_value.isFunctionObjId()) typeInfo = "function_obj_id";
    else if (callee_value.isClosureId()) typeInfo = "closure_id (unexpected)";
    COMPILER_THROW("CALL expects function or closure as callee (got " + typeInfo + ")");
  }

  if (!current_chunk) {
    COMPILER_THROW("No chunk available for function call");
  }
  
  const auto *callee = current_chunk->getFunction(function_index);
  if (!callee) {
    COMPILER_THROW("Function index not found: " +
                             std::to_string(function_index));
  }

  // Debug
  (void)callee_value;

  // Allow fewer arguments than parameters (for default parameters)
  // For variadic functions, allow MORE arguments than parameters
  if (callee->variadic_param_index == UINT32_MAX &&
      args.size() > callee->param_count) {
    COMPILER_THROW("Argument count mismatch calling function index " +
                             std::to_string(function_index) +
                             " (expected at most " +
                             std::to_string(callee->param_count) + ", got " +
                             std::to_string(args.size()) + ")");
  }

  // For variadic functions, require at least as many args as non-variadic
  // params
  if (callee->variadic_param_index != UINT32_MAX &&
      args.size() < callee->variadic_param_index) {
    COMPILER_THROW("Argument count mismatch calling function index " +
                             std::to_string(function_index) +
                             " (expected at least " +
                             std::to_string(callee->variadic_param_index) +
                             ", got " + std::to_string(args.size()) + ")");
  }

  // Advance caller IP now so RETURN resumes at the next instruction.
  if (advance_caller_ip && frame_count_ > 0) {
    currentFrame().ip++;
  }

  size_t base = locals.size();
  locals.resize(base + callee->local_count, nullptr);
  if (frame_arena_.size() <= frame_count_) {
    frame_arena_.push_back(CallFrame{callee, 0, base, closure_id});
  } else {
    frame_arena_[frame_count_] = CallFrame{callee, 0, base, closure_id};
  }
  frame_count_++;

  // Initialize parameter slots: provided args first, then defaults
  // Handle variadic parameters: pack extra args into array
  for (uint32_t i = 0; i < callee->param_count; i++) {
    if (callee->variadic_param_index != UINT32_MAX &&
        i == callee->variadic_param_index) {
      // Variadic parameter: pack remaining args into array
      auto arrRef = heap_.allocateArray();
      auto *arr = heap_.array(arrRef.id);
      for (size_t j = i; j < args.size(); j++) {
        arr->push_back(std::move(args[j]));
      }
      locals[base + i] = Value::makeArrayId(arrRef.id);
    } else if (i < args.size()) {
      locals[base + i] = std::move(args[i]);
    } else if (i < callee->default_values.size() &&
               callee->default_values[i].has_value()) {
      locals[base + i] = callee->default_values[i].value();
    } else {
      locals[base + i] = nullptr; // No arg provided, no default
    }
  }
}

void VM::doTailCall(Value callee_value,
                    std::vector<Value> args) {
  // Tail call optimization: reuse current frame instead of pushing new one
  if (callee_value.isHostFuncId()) {
    // TODO: host func name lookup
    (void)callee_value.asHostFuncId();
    COMPILER_THROW("Host function tail call not yet supported with NaN boxing");
  }

  uint32_t function_index = 0;
  uint32_t closure_id = 0;
  if (callee_value.isFunctionObjId()) {
    function_index = callee_value.asFunctionObjId();
  } else if (callee_value.isClosureId()) {
    closure_id = callee_value.asClosureId();
    auto *closure = heap_.closure(closure_id);
    if (!closure) {
      COMPILER_THROW("Closure not found: " +
                               std::to_string(closure_id));
    }
    function_index = closure->function_index;
  } else {
    COMPILER_THROW("TAIL_CALL expects function or closure as callee");
  }

  if (!current_chunk) {
    COMPILER_THROW("No chunk available for tail call");
  }

  const auto *callee = current_chunk->getFunction(function_index);
  if (!callee) {
    COMPILER_THROW("Function index not found: " +
                             std::to_string(function_index));
  }

  // Allow fewer arguments than parameters (for default parameters)
  // For variadic functions, allow MORE arguments than parameters
  if (callee->variadic_param_index == UINT32_MAX &&
      args.size() > callee->param_count) {
    COMPILER_THROW(
        "Argument count mismatch for tail call to function index " +
        std::to_string(function_index) + " (expected at most " +
        std::to_string(callee->param_count) + ", got " +
        std::to_string(args.size()) + ")");
  }

  // For variadic functions, require at least as many args as non-variadic
  // params
  if (callee->variadic_param_index != UINT32_MAX &&
      args.size() < callee->variadic_param_index) {
    COMPILER_THROW(
        "Argument count mismatch for tail call to function index " +
        std::to_string(function_index) + " (expected at least " +
        std::to_string(callee->variadic_param_index) + ", got " +
        std::to_string(args.size()) + ")");
  }

  // TCO: Reuse current frame - update function, reset IP, adjust locals
  auto &current_frame = currentFrame();
  size_t old_base = current_frame.locals_base;

  // Update frame to point to new function
  current_frame.function = callee;
  current_frame.ip = 0;
  current_frame.closure_id = closure_id;
  // Keep same locals base

  // Resize locals if needed (reuse existing space)
  size_t new_locals_needed = old_base + callee->local_count;
  if (locals.size() < new_locals_needed) {
    locals.resize(new_locals_needed, nullptr);
  }

  // Set up arguments in the reused frame (at old_base): provided args first,
  // then defaults Handle variadic parameters: pack extra args into array
  for (uint32_t i = 0; i < callee->param_count; i++) {
    if (callee->variadic_param_index != UINT32_MAX &&
        i == callee->variadic_param_index) {
      // Variadic parameter: pack remaining args into array
      auto arrRef = heap_.allocateArray();
      auto *arr = heap_.array(arrRef.id);
      for (size_t j = i; j < args.size(); j++) {
        arr->push_back(std::move(args[j]));
      }
      locals[old_base + i] = Value::makeArrayId(arrRef.id);
    } else if (i < args.size()) {
      locals[old_base + i] = std::move(args[i]);
    } else if (i < callee->default_values.size() &&
               callee->default_values[i].has_value()) {
      locals[old_base + i] = callee->default_values[i].value();
    } else {
      locals[old_base + i] = nullptr;
    }
  }

  // Clear remaining locals from old function
  for (size_t i = old_base + args.size(); i < new_locals_needed; i++) {
    locals[i] = nullptr;
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

std::vector<Value> VM::stackValuesForRoots() const {
  std::vector<Value> values;
  std::stack<Value> copy = stack;
  values.reserve(copy.size());
  while (!copy.empty()) {
    values.push_back(copy.top());
    copy.pop();
  }
  return values;
}

std::vector<uint32_t> VM::activeClosureIdsForRoots() const {
  std::vector<uint32_t> closure_ids;
  closure_ids.reserve(frame_count_);
  for (size_t i = 0; i < frame_count_; ++i) {
    const auto &frame = frame_arena_[i];
    if (frame.closure_id != 0) {
      closure_ids.push_back(frame.closure_id);
    }
  }
  return closure_ids;
}

void VM::maybeCollectGarbage() {
  heap_.maybeCollectGarbage(
      stackValuesForRoots(), locals, globals, activeClosureIdsForRoots(),
      [this](uint32_t index) -> std::optional<Value> {
        if (index >= locals.size()) {
          return std::nullopt;
        }
        return locals[index];
      });
}

void VM::collectGarbage() {
  heap_.collectGarbage(stackValuesForRoots(), locals, globals,
                       activeClosureIdsForRoots(),
                       [this](uint32_t index) -> std::optional<Value> {
                         if (index >= locals.size()) {
                           return std::nullopt;
                         }
                         return locals[index];
                       });
}

// ============================================================================
// Stack helpers - extracted from executeInstruction to reduce stack frame size
// ============================================================================

Value VM::popStack() {
  if (stack.empty()) {
    COMPILER_THROW("Stack underflow");
  }
  Value value = stack.top();
  stack.pop();
  return value;
}

void VM::pushStack(Value value) { stack.push(std::move(value)); }

uint32_t VM::toAbsoluteLocal(uint32_t local_index) {
  return static_cast<uint32_t>(currentFrame().locals_base + local_index);
}

void VM::ensureLocalIndex(uint32_t absolute_index) {
  if (absolute_index >= locals.size()) {
    locals.resize(static_cast<size_t>(absolute_index) + 1, nullptr);
  }
}

void VM::doReturn() {
  if (frame_count_ == 0) {
    return;
  }

  Value ret = nullptr;
  if (!stack.empty()) {
    ret = popStack();
  }

  auto finished = frame_arena_[frame_count_ - 1];
  frame_count_--;

  closeFrameUpvalues(static_cast<uint32_t>(finished.locals_base),
                     static_cast<uint32_t>(locals.size()));

  if (locals.size() >= finished.locals_base) {
    locals.resize(finished.locals_base);
  }

  pushStack(ret);
}

// ============================================================================
// Extracted opcode handlers to reduce executeInstruction stack frame
// ============================================================================

void VM::execBinaryOp(const Instruction &instruction) {
  Value right = popStack();
  Value left = popStack();

  // Handle null comparisons explicitly
  if (isNull(left) || isNull(right)) {
    bool result = false;
    switch (instruction.opcode) {
    case OpCode::EQ:
      result = isNull(left) && isNull(right);
      break;
    case OpCode::NEQ:
      result = !(isNull(left) && isNull(right));
      break;
    case OpCode::LT:
    case OpCode::LTE:
    case OpCode::GT:
    case OpCode::GTE:
      result = false;
      break;
    case OpCode::ADD:
    case OpCode::SUB:
    case OpCode::MUL:
    case OpCode::DIV:
    case OpCode::MOD:
    case OpCode::POW:
      // Arithmetic with null results in null (not an error)
      pushStack(Value::makeNull());
      return;
    default:
      COMPILER_THROW("Invalid operation opcode with null");
    }
    pushStack(result);
    return;
  }

  if (instruction.opcode == OpCode::EQ || instruction.opcode == OpCode::NEQ) {
    const bool equal = valuesEqualDeep(left, right);
    pushStack(instruction.opcode == OpCode::EQ ? equal : !equal);
    return;
  }

  if (left.isInt() && right.isInt()) {
    int64_t l = left.asInt();
    int64_t r = right.asInt();
    switch (instruction.opcode) {
    case OpCode::ADD:  pushStack(l + r); break;
    case OpCode::SUB:  pushStack(l - r); break;
    case OpCode::MUL:  pushStack(l * r); break;
    case OpCode::DIV:
      if (r == 0) throw ScriptThrow{Value("Division by zero")};
      pushStack(l / r);
      break;
    case OpCode::MOD:
      if (r == 0) throw ScriptThrow{Value("Modulo by zero")};
      pushStack(l % r);
      break;
    case OpCode::POW:
      pushStack(static_cast<int64_t>(
          std::pow(static_cast<double>(l), static_cast<double>(r))));
      break;
    case OpCode::EQ:   pushStack(l == r); break;
    case OpCode::NEQ:  pushStack(l != r); break;
    case OpCode::LT:   pushStack(l < r); break;
    case OpCode::LTE:  pushStack(l <= r); break;
    case OpCode::GT:   pushStack(l > r); break;
    case OpCode::GTE:  pushStack(l >= r); break;
    default: COMPILER_THROW("Unsupported integer operation");
    }
    return;
  }

  if ((left.isInt() || left.isDouble()) &&
      (right.isInt() || right.isDouble())) {
    double l = left.isInt() ? static_cast<double>(left.asInt()) : left.asDouble();
    double r = right.isInt() ? static_cast<double>(right.asInt()) : right.asDouble();
    switch (instruction.opcode) {
    case OpCode::ADD:  pushStack(l + r); break;
    case OpCode::SUB:  pushStack(l - r); break;
    case OpCode::MUL:  pushStack(l * r); break;
    case OpCode::DIV:
      if (r == 0.0) throw ScriptThrow{Value("Division by zero")};
      pushStack(l / r);
      break;
    case OpCode::MOD:
      if (r == 0.0) COMPILER_THROW("Modulo by zero");
      pushStack(std::fmod(l, r));
      break;
    case OpCode::POW:  pushStack(std::pow(l, r)); break;
    case OpCode::EQ:   pushStack(l == r); break;
    case OpCode::NEQ:  pushStack(l != r); break;
    case OpCode::LT:   pushStack(l < r); break;
    case OpCode::LTE:  pushStack(l <= r); break;
    case OpCode::GT:   pushStack(l > r); break;
    case OpCode::GTE:  pushStack(l >= r); break;
    default: COMPILER_THROW("Unsupported floating point operation");
    }
    return;
  }

  // Handle string operations (StringValId = compile-time constant, StringId = runtime string)
  if (left.isStringValId() || left.isStringId() || right.isStringValId() || right.isStringId()) {
    // Resolve left operand to actual string
    std::string l;
    if (left.isStringValId()) {
      // Try to resolve from current chunk's string table
      if (current_chunk) {
        l = current_chunk->getString(left.asStringValId());
      } else {
        l = "<string:" + std::to_string(left.asStringValId()) + ">";
      }
    } else if (left.isStringId()) {
      if (auto *s = heap_.string(left.asStringId())) {
        l = *s;
      } else {
        l = "<string:" + std::to_string(left.asStringId()) + ">";
      }
    } else if (left.isInt()) {
      l = std::to_string(left.asInt());
    } else if (left.isDouble()) {
      l = std::to_string(left.asDouble());
    } else if (left.isBool()) {
      l = left.asBool() ? "true" : "false";
    } else {
      l = left.toString();
    }

    // Resolve right operand to actual string
    std::string r;
    if (right.isStringValId()) {
      if (current_chunk) {
        r = current_chunk->getString(right.asStringValId());
      } else {
        r = "<string:" + std::to_string(right.asStringValId()) + ">";
      }
    } else if (right.isStringId()) {
      if (auto *s = heap_.string(right.asStringId())) {
        r = *s;
      } else {
        r = "<string:" + std::to_string(right.asStringId()) + ">";
      }
    } else if (right.isInt()) {
      r = std::to_string(right.asInt());
    } else if (right.isDouble()) {
      r = std::to_string(right.asDouble());
    } else if (right.isBool()) {
      r = right.asBool() ? "true" : "false";
    } else {
      r = right.toString();
    }

    switch (instruction.opcode) {
    case OpCode::ADD: {
      std::string result = l + r;
      auto strRef = heap_.allocateString(std::move(result));
      pushStack(Value::makeStringId(strRef.id));
      break;
    }
    case OpCode::EQ:   pushStack(l == r); break;
    case OpCode::NEQ:  pushStack(l != r); break;
    default: COMPILER_THROW("Invalid string operation");
    }
    return;
  }

  COMPILER_THROW("Type mismatch in binary operation");
}

void VM::execLogicalOp(OpCode opcode) {
  Value right = popStack();
  Value left = popStack();
  switch (opcode) {
  case OpCode::AND: pushStack(isTruthy(left) && isTruthy(right)); break;
  case OpCode::OR:  pushStack(isTruthy(left) || isTruthy(right)); break;
  default: COMPILER_THROW("Unknown logical opcode");
  }
}

void VM::execNegate() {
  Value value = popStack();
  if (value.isInt()) {
    pushStack(-value.asInt());
  } else if (value.isDouble()) {
    pushStack(-value.asDouble());
  } else {
    COMPILER_THROW("Cannot negate non-numeric value");
  }
}

void VM::execJump(const Instruction &instruction) {
  uint32_t target = instruction.operands[0].asInt();
  currentFrame().ip = target;
}

void VM::execJumpIfFalse(const Instruction &instruction) {
  uint32_t target = instruction.operands[0].asInt();
  Value condition = popStack();
  if (!isTruthy(condition)) {
    currentFrame().ip = target;
  }
}

void VM::execJumpIfTrue(const Instruction &instruction) {
  uint32_t target = instruction.operands[0].asInt();
  Value condition = popStack();
  if (isTruthy(condition)) {
    currentFrame().ip = target;
  }
}

// ============================================================================
// Main executeInstruction dispatcher
// ============================================================================

void VM::executeInstruction(const Instruction &instruction) {
  switch (instruction.opcode) {
  case OpCode::LOAD_CONST: {
    uint32_t const_index = instruction.operands[0].asInt();
    pushStack(getConstant(const_index));
    break;
  }

  case OpCode::LOAD_GLOBAL: {
    if (instruction.operands.empty() ||
        !instruction.operands[0].isStringValId()) {
      COMPILER_THROW("LOAD_GLOBAL expects string operand");
    }
    // Get the string from the function's string table
    uint32_t strIndex = instruction.operands[0].asStringValId();
    const auto* func = currentFrame().function;
    std::string name;
    if (current_chunk) {
      name = current_chunk->getString(strIndex);
    } else {
      name = "<unknown:" + std::to_string(strIndex) + ">";
    }

    // First check regular globals (user variables shadow host functions)
    auto it = globals.find(name);
    if (it != globals.end()) {
      pushStack(it->second);
      break;
    }

    // Then check host function globals (fallback for built-in functions)
    auto hostIt = host_function_globals_.find(name);
    if (hostIt != host_function_globals_.end()) {
      pushStack(hostIt->second);
      break;
    }

    COMPILER_THROW("Undefined variable: '" + name + "'");
    break;
  }

  case OpCode::STORE_GLOBAL: {
    if (instruction.operands.empty() ||
        !instruction.operands[0].isStringValId()) {
      COMPILER_THROW("STORE_GLOBAL expects string operand");
    }
    // Get the string from the function's string table
    uint32_t strIndex = instruction.operands[0].asStringValId();
    std::string name;
    if (current_chunk) {
      name = current_chunk->getString(strIndex);
    } else {
      name = "<unknown:" + std::to_string(strIndex) + ">";
    }
    Value value = popStack();


    // Value type semantics: copy structs on assignment
    if (value.isStructId()) {
      StructRef ref{value.asStructId(), value.asStructTypeId()};
      StructRef copy = copyStruct(ref);
      value = Value::makeStructId(copy.id, copy.typeId);
    }

    globals[name] = value;
    break;
  }

  case OpCode::LOAD_VAR: {
    uint32_t var_index = instruction.operands[0].asInt();
    uint32_t abs = this->toAbsoluteLocal(var_index);
    this->ensureLocalIndex(abs);
    Value value = locals[abs];

    // Value type semantics: copy structs on load
    if (value.isStructId()) {
      StructRef ref{value.asStructId(), value.asStructTypeId()};
      StructRef copy = copyStruct(ref);
      value = Value::makeStructId(copy.id, copy.typeId);
    }

    pushStack(value);
    break;
  }

  case OpCode::STORE_VAR: {
    uint32_t var_index = instruction.operands[0].asInt();
    uint32_t abs = this->toAbsoluteLocal(var_index);
    this->ensureLocalIndex(abs);
    Value value = popStack();

    // Value type semantics: copy structs on assignment
    if (value.isStructId()) {
      StructRef ref{value.asStructId(), value.asStructTypeId()};
      StructRef copy = copyStruct(ref);
      value = Value::makeStructId(copy.id, copy.typeId);
    }

    locals[abs] = value;
    break;
  }

  case OpCode::LOAD_UPVALUE: {
    uint32_t upvalue_index = instruction.operands[0].asInt();
    uint32_t closure_id = currentFrame().closure_id;
    if (closure_id == 0) {
      COMPILER_THROW("LOAD_UPVALUE used without active closure");
    }
    auto *closure = heap_.closure(closure_id);
    if (!closure) {
      COMPILER_THROW("Closure not found for LOAD_UPVALUE");
    }
    if (upvalue_index >= closure->upvalues.size() ||
        !closure->upvalues[upvalue_index]) {
      COMPILER_THROW("LOAD_UPVALUE index out of range");
    }
    const auto &cell = closure->upvalues[upvalue_index];
    Value value;
    if (cell->is_open) {
      this->ensureLocalIndex(cell->open_index);
      value = locals[cell->open_index];

      // Value type semantics: copy structs on load
      if (value.isStructId()) {
        StructRef ref{value.asStructId(), value.asStructTypeId()};
        StructRef copy = copyStruct(ref);
        value = Value::makeStructId(copy.id, copy.typeId);
      }
    } else {
      value = cell->closed_value;
    }
    pushStack(value);
    break;
  }

  case OpCode::STORE_UPVALUE: {
    uint32_t upvalue_index = instruction.operands[0].asInt();
    uint32_t closure_id = currentFrame().closure_id;
    if (closure_id == 0) {
      COMPILER_THROW("STORE_UPVALUE used without active closure");
    }
    auto *closure = heap_.closure(closure_id);
    if (!closure) {
      COMPILER_THROW("Closure not found for STORE_UPVALUE");
    }
    if (upvalue_index >= closure->upvalues.size() ||
        !closure->upvalues[upvalue_index]) {
      COMPILER_THROW("STORE_UPVALUE index out of range");
    }
    auto &cell = closure->upvalues[upvalue_index];
    Value value = popStack();

    // Value type semantics: copy structs on assignment
    if (value.isStructId()) {
      StructRef ref{value.asStructId(), value.asStructTypeId()};
      StructRef copy = copyStruct(ref);
      value = Value::makeStructId(copy.id, copy.typeId);
    }

    if (cell->is_open) {
      this->ensureLocalIndex(cell->open_index);
      locals[cell->open_index] = value;
    } else {
      cell->closed_value = value;
    }
    break;
  }

  case OpCode::POP: {
    popStack();
    break;
  }

  case OpCode::DUP: {
    Value value = popStack();
    pushStack(value);
    pushStack(value);
    break;
  }

  case OpCode::SWAP: {
    Value top = popStack();
    Value next = popStack();
    pushStack(top);
    pushStack(next);
    break;
  }

  case OpCode::PUSH_NULL: {
    pushStack(Value::makeNull());
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
  case OpCode::GTE:
    execBinaryOp(instruction);
    break;

  case OpCode::AND:
  case OpCode::OR:
    execLogicalOp(instruction.opcode);
    break;

  case OpCode::NOT: {
    Value v = popStack();
    pushStack(!isTruthy(v));
    break;
  }

  case OpCode::NEGATE:
    execNegate();
    break;

  case OpCode::JUMP:
    execJump(instruction);
    break;

  case OpCode::JUMP_IF_FALSE:
    execJumpIfFalse(instruction);
    break;

  case OpCode::JUMP_IF_TRUE:
    execJumpIfTrue(instruction);
    break;

  case OpCode::IS_NULL: {
    Value value = popStack();
    bool isNullVal = value.isNull();
    pushStack(Value::makeBool(isNullVal));
    break;
  }

  case OpCode::JUMP_IF_NULL: {
    uint32_t target = instruction.operands[0].asInt();
    Value value = popStack();
    // Only jump on null/undefined, not on all falsy values
    if (value.isNull()) {
      currentFrame().ip = target;
    }
    break;
  }

  case OpCode::CALL: {
    uint32_t arg_count = instruction.operands[0].asInt();
    if (stack.size() < static_cast<size_t>(arg_count) + 1) {
      COMPILER_THROW("Stack underflow during CALL");
    }

    std::vector<Value> args(arg_count);
    for (uint32_t i = 0; i < arg_count; ++i) {
      args[arg_count - 1 - i] = popStack();
    }
    Value callee_value = popStack();

    // Debug: check what type the callee is
    std::string typeInfo = "unknown";
    if (callee_value.isNull()) typeInfo = "null";
    else if (callee_value.isInt()) typeInfo = "int";
    else if (callee_value.isClosureId()) typeInfo = "closure_id";
    else if (callee_value.isFunctionObjId()) typeInfo = "function_obj_id";
    else if (callee_value.isObjectId()) typeInfo = "object_id";
    else if (callee_value.isHostFuncId()) typeInfo = "host_func_id";
    
    
    // Debug: check argument types
    for (uint32_t i = 0; i < arg_count; ++i) {
      std::string argType = "unknown";
      if (args[i].isNull()) argType = "null";
      else if (args[i].isInt()) argType = "int";
      else if (args[i].isClosureId()) argType = "closure_id";
      else if (args[i].isFunctionObjId()) argType = "function_obj_id";
      else if (args[i].isObjectId()) argType = "object_id";
      else if (args[i].isHostFuncId()) argType = "host_func_id";
    }

    // Handle bound method objects (from runtime member lookup)
    if (callee_value.isObjectId()) {
      auto *obj = heap_.object(callee_value.asObjectId());
      if (obj) {
        auto fnIt = obj->find("fn");
        auto selfIt = obj->find("self");
        if (fnIt != obj->end() && selfIt != obj->end() &&
            (fnIt->second.isHostFuncId() || fnIt->second.isFunctionObjId() ||
             fnIt->second.isClosureId())) {
          // Prepend self to args
          std::vector<Value> boundArgs;
          boundArgs.push_back(selfIt->second);
          boundArgs.insert(boundArgs.end(), args.begin(), args.end());
          doCall(fnIt->second, std::move(boundArgs));
          break;
        }
      }
    }

    doCall(callee_value, std::move(args));
    break;
  }

  case OpCode::TAIL_CALL: {
    // Tail call optimization: reuse current frame instead of pushing new one
    uint32_t arg_count = instruction.operands[0].asInt();
    if (stack.size() < static_cast<size_t>(arg_count) + 1) {
      COMPILER_THROW("Stack underflow during TAIL_CALL");
    }

    std::vector<Value> args(arg_count);
    for (uint32_t i = 0; i < arg_count; ++i) {
      args[arg_count - 1 - i] = popStack();
    }
    Value callee_value = popStack();

    doTailCall(callee_value, std::move(args));
    break;
  }

  case OpCode::CALL_HOST: {
    if (instruction.operands.size() != 2 ||
        !instruction.operands[0].isStringValId() ||
        !instruction.operands[1].isInt()) {
      COMPILER_THROW("CALL_HOST expects operands: <string "
                               "function_name, uint32 arg_count>");
    }

    // Get the function name from the function's string table
    uint32_t strIndex = instruction.operands[0].asStringValId();
    const auto* func = currentFrame().function;
    std::string function_name;
    if (current_chunk) {
      function_name = current_chunk->getString(strIndex);
    } else {
      function_name = "<unknown:" + std::to_string(strIndex) + ">";
    }
    uint32_t arg_count = instruction.operands[1].asInt();
    
    auto result = invokeHostFunction(function_name, arg_count);
    pushStack(result);
    break;
  }

  case OpCode::CALL_SUPER: {
    // CALL_SUPER: operands are [method_name, arg_count]
    // Pops args from stack, looks up parent class method, calls it
    if (instruction.operands.size() != 2 ||
        !instruction.operands[0].isStringValId() ||
        !instruction.operands[1].isInt()) {
      COMPILER_THROW("CALL_SUPER expects operands: <string "
                               "method_name, uint32 arg_count>");
    }

    const std::string &method_name =
        instruction.operands[0].toString();
    uint32_t arg_count = instruction.operands[1].asInt();

    if (stack.size() < static_cast<size_t>(arg_count)) {
      COMPILER_THROW("Stack underflow during CALL_SUPER");
    }

    // Pop arguments from stack
    std::vector<Value> args(arg_count);
    for (uint32_t i = 0; i < arg_count; ++i) {
      args[arg_count - 1 - i] = popStack();
    }

    // Get current 'this' from local scope (slot 0 typically)
    size_t base = currentFrame().locals_base;
    Value this_value = locals[base + 0];

    // Find the parent class method using the prototype chain
    // For now, emit as a host function call with special prefix
    // Full implementation needs parent method lookup via heap_.findClassMethod
    std::string super_method_name = "super." + method_name;

    // Prepend 'this' to args
    args.insert(args.begin(), this_value);

    // Call as host function - runtime will need to resolve via parent class
    pushStack(invokeHostFunction(super_method_name,
                            static_cast<uint32_t>(args.size())));
    break;
  }

  case OpCode::RETURN: {
    // Debug: check what type is being returned
    if (!stack.empty()) {
      Value ret = stack.top();
      std::string typeInfo = "unknown";
      if (ret.isNull()) typeInfo = "null";
      else if (ret.isInt()) typeInfo = "int";
      else if (ret.isClosureId()) typeInfo = "closure_id";
      else if (ret.isFunctionObjId()) typeInfo = "function_obj_id";
      else if (ret.isObjectId()) typeInfo = "object_id";
    }
    this->doReturn();
    break;
  }

  case OpCode::TRY_ENTER: {
    if (instruction.operands.size() < 1 ||
        !instruction.operands[0].isInt()) {
      COMPILER_THROW("TRY_ENTER expects catch ip operand");
    }
    const uint32_t catch_ip = instruction.operands[0].asInt();
    uint32_t finally_ip = 0;
    if (instruction.operands.size() >= 2 &&
        instruction.operands[1].isInt()) {
      finally_ip = instruction.operands[1].asInt();
    }
    currentFrame().try_stack.push_back(TryHandler{.catch_ip = catch_ip,
                                                  .finally_ip = finally_ip,
                                                  .finally_return_ip = 0,
                                                  .stack_depth = stack.size()});
    break;
  }

  case OpCode::TRY_EXIT: {
    if (!currentFrame().try_stack.empty()) {
      currentFrame().try_stack.pop_back();
    }
    break;
  }

  case OpCode::LOAD_EXCEPTION: {
    if (has_current_exception_) {
      pushStack(current_exception_);
    } else {
      pushStack(nullptr);
    }
    break;
  }

  case OpCode::THROW: {
    Value thrown = popStack();
    throw ScriptThrow{std::move(thrown)};
  }

  case OpCode::CLOSURE: {
    uint32_t function_index = instruction.operands[0].asInt();
    const auto *target = current_chunk->getFunction(function_index);
    if (!target) {
      COMPILER_THROW("CLOSURE references unknown function index");
    }

    RuntimeClosure closure;
    closure.function_index = function_index;
    closure.upvalues.reserve(target->upvalues.size());
    for (const auto &descriptor : target->upvalues) {
      if (descriptor.captures_local) {
        uint32_t abs = this->toAbsoluteLocal(descriptor.index);
        this->ensureLocalIndex(abs);
        auto open_it = open_upvalues.find(abs);
        if (open_it == open_upvalues.end()) {
          auto cell = std::make_shared<GCHeap::UpvalueCell>();
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
          COMPILER_THROW(
              "CLOSURE tried to capture upvalue without parent closure");
        }
        auto *parent_closure = heap_.closure(parent_closure_id);
        if (!parent_closure) {
          COMPILER_THROW("Parent closure not found for CLOSURE");
        }
        if (descriptor.index >= parent_closure->upvalues.size()) {
          COMPILER_THROW("CLOSURE upvalue index out of range");
        }
        closure.upvalues.push_back(parent_closure->upvalues[descriptor.index]);
      }
    }

    pushStack(Value::makeClosureId(heap_.allocateClosure(
        GCHeap::RuntimeClosure{.function_index = closure.function_index,
                               .upvalues = std::move(closure.upvalues)}).id));
    // Disable GC to test if it's causing corruption
    // maybeCollectGarbage();
    break;
  }

  case OpCode::ARRAY_NEW: {
    
    Value arr = Value::makeArrayId(heap_.allocateArray().id);
    
    pushStack(arr);
    maybeCollectGarbage();
    break;
  }

  case OpCode::SET_NEW: {
    pushStack(Value::makeSetId(heap_.allocateSet().id));
    maybeCollectGarbage();
    break;
  }

  case OpCode::ARRAY_PUSH: {
    Value value = popStack();
    Value container = popStack();
    
    if (!container.isArrayId()) {
      COMPILER_THROW("ARRAY_PUSH expects array container");
    }
    uint32_t id = container.asArrayId();
    auto *array = heap_.array(id);
    if (!array) {
      COMPILER_THROW("ARRAY_PUSH unknown array id");
    }
    array->push_back(value);
    pushStack(container);
    break;
  }

  case OpCode::ARRAY_LEN: {
    Value container = popStack();
    if (!container.isArrayId()) {
      COMPILER_THROW("ARRAY_LEN expects array container");
    }
    uint32_t id = container.asArrayId();
    auto *array = heap_.array(id);
    if (!array) {
      COMPILER_THROW("ARRAY_LEN unknown array id");
    }
    pushStack(Value::makeInt(static_cast<int64_t>(array->size())));
    break;
  }

  // Range creation: start..end or start..step..end
  case OpCode::RANGE_NEW: {
    int64_t end = popStack().asInt();
    int64_t start = popStack().asInt();
    RangeRef rangeRef = heap_.allocateRange(start, end, 1);
    pushStack(Value::makeRangeId(rangeRef.id));
    break;
  }

  case OpCode::RANGE_STEP_NEW: {
    int64_t step = popStack().asInt();
    int64_t end = popStack().asInt();
    int64_t start = popStack().asInt();
    RangeRef rangeRef = heap_.allocateRange(start, end, step);
    pushStack(Value::makeRangeId(rangeRef.id));
    break;
  }

  // Struct operations
  case OpCode::STRUCT_NEW: {
    // Operands: typeId (uint32), fieldCount (uint32)
    uint32_t typeId = instruction.operands[0].asInt();
    uint32_t fieldCount = instruction.operands[1].asInt();
    StructRef structRef = heap_.allocateStruct(typeId, fieldCount);
    struct_instance_type_ids_[structRef.id] = typeId;
    pushStack(Value::makeStructId(structRef.id, structRef.typeId));
    break;
  }

  case OpCode::STRUCT_GET: {
    // Pop: struct ref, index
    Value indexVal = popStack();
    Value structVal = popStack();
    if (!structVal.isStructId() || !indexVal.isInt()) {
      COMPILER_THROW("STRUCT_GET expects struct and int index");
    }
    size_t index = static_cast<size_t>(indexVal.asInt());
    pushStack(heap_.structs_.at(structVal.asStructId()).at(index));
    break;
  }

  case OpCode::STRUCT_SET: {
    // Pop: struct ref, index, value
    Value value = popStack();
    Value indexVal = popStack();
    Value structVal = popStack();
    if (!structVal.isStructId() || !indexVal.isInt()) {
      COMPILER_THROW(
          "STRUCT_SET expects struct, int index, and value");
    }
    auto structRef = StructRef{structVal.asStructId()};
    size_t index = static_cast<size_t>(indexVal.asInt());
    heap_.structs_.at(structRef.id).at(index) = value;
    break;
  }

  // Enum operations
  case OpCode::ENUM_NEW: {
    // Operands: typeId (uint32), tag (uint32), payloadCount (uint32)
    uint32_t typeId = instruction.operands[0].asInt();
    uint32_t tag = instruction.operands[1].asInt();
    uint32_t payloadCount = instruction.operands[2].asInt();
    EnumRef enumRef = heap_.allocateEnum(typeId, tag, payloadCount);
    pushStack(Value::makeEnumId(enumRef.id));
    break;
  }

  case OpCode::ENUM_TAG: {
    Value enumVal = popStack();
    if (!enumVal.isEnumId()) {
      COMPILER_THROW("ENUM_TAG expects enum");
    }
    auto enumRef = EnumRef{enumVal.asEnumId(), 0, 0};
    pushStack(Value::makeInt(static_cast<int64_t>(enumRef.tag)));
    break;
  }

  case OpCode::ENUM_PAYLOAD: {
    Value indexVal = popStack();
    Value enumVal = popStack();
    if (!enumVal.isEnumId() || !indexVal.isInt()) {
      COMPILER_THROW("ENUM_PAYLOAD expects enum and int index");
    }
    auto enumRef = EnumRef{enumVal.asEnumId(), 0, 0};
    size_t index = static_cast<size_t>(indexVal.asInt());
    pushStack(heap_.enums_.at(enumRef.id).second.at(index));
    break;
  }

  case OpCode::ENUM_MATCH: {
    // Pop: enum ref, expected tag
    Value tagVal = popStack();
    Value enumVal = popStack();
    if (!enumVal.isEnumId() || !tagVal.isInt()) {
      COMPILER_THROW("ENUM_MATCH expects enum and int tag");
    }
    auto enumRef = EnumRef{enumVal.asEnumId(), 0, 0};
    int64_t expectedTag = tagVal.asInt();
    pushStack(Value::makeBool(enumRef.tag == static_cast<uint32_t>(expectedTag)));
    break;
  }

  // String promotion: StringValId → StringId (for runtime string iteration)
  case OpCode::STRING_PROMOTE: {
    Value v = popStack();
    if (v.isStringValId()) {
      // Look up string from chunk's string table and allocate on heap
      uint32_t strIdx = v.asStringValId();
      std::string s;
      if (current_chunk) {
        s = current_chunk->getString(strIdx);
      }
      auto strRef = heap_.allocateString(s);
      pushStack(Value::makeStringId(strRef.id));
    } else {
      // Already a StringId or other type, passthrough
      pushStack(v);
    }
    break;
  }

  // Iteration protocol: iter(obj) → iterator
  case OpCode::ITER_NEW: {
    Value iterable = popStack();

    // Create iterator based on type
    IteratorRef iterRef;
    iterRef.id = heap_.createIterator(iterable);
    pushStack(Value::makeIteratorId(iterRef.id));
    break;
  }

  // Iteration protocol: iterator.next() → {value, done}
  case OpCode::ITER_NEXT: {
    Value iterator_val = popStack();
    if (!iterator_val.isIteratorId()) {
      COMPILER_THROW("ITER_NEXT expects iterator");
    }

    uint32_t id = iterator_val.asIteratorId();
    auto result = heap_.iteratorNext(id);

    // result is {value, done} object
    pushStack(result);
    break;
  }

  case OpCode::ARRAY_GET: {
    Value index_or_key = popStack();
    Value container = popStack();

    if (container.isArrayId()) {
      auto index = indexFromValue(index_or_key);
      if (!index) {
        COMPILER_THROW("ARRAY_GET expects integer index");
      }
      auto *array = heap_.array(container.asArrayId());
      if (!array) {
        COMPILER_THROW("ARRAY_GET unknown array id");
      }
      // Handle negative indices: -1 = last element, -2 = second to last, etc.
      int64_t idx = *index;
      if (idx < 0) {
        idx = static_cast<int64_t>(array->size()) + idx;
      }
      if (idx < 0 || static_cast<size_t>(idx) >= array->size()) {
        pushStack(Value::makeNull());
      } else {
        pushStack((*array)[static_cast<size_t>(idx)]);
      }
      break;
    }

    if (container.isSetId()) {
      auto key = keyFromValue(index_or_key, &heap_, current_chunk);
      if (!key) {
        COMPILER_THROW(
            "SET membership expects string/number/bool key");
      }
      auto *set = heap_.set(container.asSetId());
      if (!set) {
        COMPILER_THROW("ARRAY_GET unknown set id");
      }
      pushStack(set->find(*key) != set->end());
      break;
    }

    if (container.isObjectId()) {
      auto key = keyFromValue(index_or_key, &heap_, current_chunk);
      if (!key) {
        COMPILER_THROW("OBJECT index expects string/number/bool key");
      }
      auto *object = heap_.object(container.asObjectId());
      if (!object) {
        COMPILER_THROW("ARRAY_GET unknown object id");
      }
      auto kv = object->find(*key);
      pushStack(kv == object->end() ? Value::makeNull() : kv->second);
      break;
    }

    COMPILER_THROW("ARRAY_GET expects array/set/object container");
  }

  case OpCode::ARRAY_SET: {
    Value value = popStack();
    Value index_or_key = popStack();
    Value container = popStack();

    if (container.isArrayId()) {
      auto index = indexFromValue(index_or_key);
      if (!index) {
        COMPILER_THROW("ARRAY_SET expects integer index");
      }
      auto *array = heap_.array(container.asArrayId());
      if (!array) {
        COMPILER_THROW("ARRAY_SET unknown array id");
      }
      // Handle negative indices: -1 = last element, etc.
      int64_t idx = *index;
      if (idx < 0) {
        idx = static_cast<int64_t>(array->size()) + idx;
      }
      if (idx < 0) {
        COMPILER_THROW("ARRAY_SET index out of bounds");
      }
      const auto idx_size = static_cast<size_t>(idx);
      if (idx_size >= array->size()) {
        array->resize(idx_size + 1, Value::makeNull());
      }
      (*array)[idx_size] = value;
      break;
    }

    if (container.isSetId()) {
      auto key = keyFromValue(index_or_key, &heap_, current_chunk);
      if (!key) {
        COMPILER_THROW(
            "SET assignment expects string/number/bool key");
      }
      auto *set = heap_.set(container.asSetId());
      if (!set) {
        COMPILER_THROW("ARRAY_SET unknown set id");
      }
      bool present = false;
      if (value.isBool()) {
        present = value.asBool();
      } else if (value.isInt()) {
        present = value.asInt() != 0;
      } else if (value.isDouble()) {
        present = value.asDouble() != 0.0;
      } else {
        COMPILER_THROW(
            "SET assignment value must be bool/number to indicate presence");
      }
      if (present) {
        (*set)[*key] = Value::makeNull();
      } else {
        set->erase(*key);
      }
      break;
    }

    if (container.isObjectId()) {
      auto key = keyFromValue(index_or_key, &heap_, current_chunk);
      if (!key) {
        COMPILER_THROW("OBJECT index assignment expects valid key");
      }
      auto *object = heap_.object(container.asObjectId());
      if (!object) {
        COMPILER_THROW("ARRAY_SET unknown object id");
      }
      (*object)[*key] = value;
      break;
    }

    COMPILER_THROW("ARRAY_SET expects array/set/object container");
  }

  case OpCode::OBJECT_NEW: {
    pushStack(Value::makeObjectId(heap_.allocateObject(true).id)); // sorted = true
    maybeCollectGarbage();
    break;
  }

  case OpCode::OBJECT_NEW_UNSORTED: {
    pushStack(Value::makeObjectId(heap_.allocateObject(false).id)); // sorted = false
    maybeCollectGarbage();
    break;
  }

  case OpCode::OBJECT_GET: {
    Value key_value = popStack();
    Value object = popStack();

    

    // Handle arrays - look up prototype methods
    if (object.isArrayId()) {
      auto key = keyFromValue(key_value, &heap_, current_chunk);
      if (key) {
        auto method = getPrototypeMethod(object, *key);
        if (method) {
          // Create a bound method object: {fn: HostFuncId, self: ArrayId}
          auto boundObj = heap_.allocateObject();
          auto *obj = heap_.object(boundObj.id);
          if (obj) {
            (*obj)["fn"] = Value::makeHostFuncId(getHostFunctionIndex(method->name));
            (*obj)["self"] = Value::makeArrayId(object.asArrayId());
          }
          pushStack(Value::makeObjectId(boundObj.id));
          break;
        }
      }
      pushStack(Value::makeNull());
      break;
    }

    if (object.isStructId()) {
      uint32_t type_id = object.asStructTypeId();
      if (type_id == 0) {
        auto it = struct_instance_type_ids_.find(object.asStructId());
        if (it != struct_instance_type_ids_.end()) {
          type_id = it->second;
        }
      }
      size_t index = 0;
      if (key_value.isInt()) {
        index = static_cast<size_t>(key_value.asInt());
        pushStack(getStructField(
            StructRef{object.asStructId(), type_id}, index));
        break;
      }
      auto key = keyFromValue(key_value, &heap_, current_chunk);
      if (!key) {
        COMPILER_THROW("OBJECT_GET expects string/number/bool key");
      }
      auto idx = heap_.structFieldIndex(type_id, *key);
      if (!idx.has_value()) {
        pushStack(Value::makeNull());
        break;
      }
      pushStack(getStructField(
          StructRef{object.asStructId(), type_id}, *idx));
      break;
    }

    if (object.isClassId()) {
      uint32_t type_id = object.asClassTypeId();
      if (type_id == 0) {
        auto it = class_instance_type_ids_.find(object.asClassId());
        if (it != class_instance_type_ids_.end()) {
          type_id = it->second;
        }
      }
      size_t index = 0;
      if (key_value.isInt()) {
        index = static_cast<size_t>(key_value.asInt());
        pushStack(getClassField(
            ClassRef{object.asClassId(), type_id}, index));
        break;
      }
      auto key = keyFromValue(key_value, &heap_, current_chunk);
      if (!key) {
        COMPILER_THROW("OBJECT_GET expects string/number/bool key");
      }
      auto idx = heap_.classFieldIndex(type_id, *key);
      if (idx.has_value()) {
        pushStack(getClassField(
            ClassRef{object.asClassId(), type_id}, *idx));
        break;
      }

      // Fallback to class method lookup (prototype chain aware).
      auto method = findClassMethod(type_id, *key);
      if (method.has_value()) {
        auto boundObj = heap_.allocateObject();
        auto *obj = heap_.object(boundObj.id);
        if (obj) {
          (*obj)["fn"] = Value::makeFunctionObjId(*method);
          (*obj)["self"] = Value::makeClassId(object.asClassId(), type_id);
        }
        pushStack(Value::makeObjectId(boundObj.id));
      } else {
        pushStack(Value::makeNull());
      }
      break;
    }

    if (!object.isObjectId()) {
      pushStack(Value::makeNull());
      break;
    }
    auto objRef = ObjectRef{object.asObjectId(), true};
    auto *obj = heap_.object(objRef.id);
    if (!obj) {
      COMPILER_THROW("OBJECT_GET unknown object id");
    }

    // Check for numeric index (obj[0], obj[-1])
    if (key_value.isInt()) {
      int64_t index = key_value.asInt();
      auto keys = obj->getKeys();
      // Handle negative indices
      if (index < 0) {
        index = static_cast<int64_t>(keys.size()) + index;
      }
      if (index >= 0 && static_cast<size_t>(index) < keys.size()) {
        auto *val = obj->get(keys[static_cast<size_t>(index)]);
        if (val) {
          pushStack(*val);
        } else {
          pushStack(Value::makeNull());
        }
      } else {
        pushStack(Value::makeNull());
      }
      break;
    }

    auto key = keyFromValue(key_value, &heap_, current_chunk);
    if (!key) {
      COMPILER_THROW("OBJECT_GET expects string/number/bool key");
    }
    auto *val = obj->get(*key);
    if (val) {
      pushStack(*val);
    } else {
      // Property not found - check prototype methods
      auto method = getPrototypeMethod(object, *key);
      if (method) {
        pushStack(Value::makeHostFuncId(0)); // TODO: proper host func id
      } else {
        pushStack(Value::makeNull());
      }
    }
    break;
  }

  case OpCode::OBJECT_SET: {
    // Stack: [..., obj, value, key] → pops all, pushes obj
    // This allows chaining: obj { DUP, val1, "k1", SET, val2, "k2", SET, ... }
    Value key = popStack();
    Value value = popStack();
    Value object = popStack();

    auto keyStr = keyFromValue(key, &heap_, current_chunk);
    if (!keyStr) {
      COMPILER_THROW("OBJECT_SET expects string/number/bool key");
    }

    // Safety: reject __ prefixed keys (reserved for internal use)
    if (keyStr->size() >= 2 && (*keyStr)[0] == '_' && (*keyStr)[1] == '_') {
      COMPILER_THROW(
          "OBJECT_SET: keys starting with '__' are reserved");
    }

    if (object.isStructId()) {
      uint32_t type_id = object.asStructTypeId();
      if (type_id == 0) {
        auto it = struct_instance_type_ids_.find(object.asStructId());
        if (it != struct_instance_type_ids_.end()) {
          type_id = it->second;
        }
      }
      size_t index = 0;
      if (key.isInt()) {
        index = static_cast<size_t>(key.asInt());
      } else {
        auto idx = heap_.structFieldIndex(type_id, *keyStr);
        if (!idx.has_value()) {
          COMPILER_THROW("Unknown struct field name: " + *keyStr);
        }
        index = *idx;
      }
      setStructField(StructRef{object.asStructId(), type_id},
                     index, value);
      pushStack(object);
      break;
    }

    if (object.isClassId()) {
      uint32_t type_id = object.asClassTypeId();
      if (type_id == 0) {
        auto it = class_instance_type_ids_.find(object.asClassId());
        if (it != class_instance_type_ids_.end()) {
          type_id = it->second;
        }
      }
      size_t index = 0;
      if (key.isInt()) {
        index = static_cast<size_t>(key.asInt());
      } else {
        auto idx = heap_.classFieldIndex(type_id, *keyStr);
        if (!idx.has_value()) {
          COMPILER_THROW("Unknown class field name: " + *keyStr);
        }
        index = *idx;
      }
      setClassField(ClassRef{object.asClassId(), type_id}, index,
                    value);
      pushStack(object);
      break;
    }

    if (!object.isObjectId()) {
      COMPILER_THROW("OBJECT_SET expects object container");
    }

    auto *obj = heap_.object(object.asObjectId());
    if (!obj) {
      COMPILER_THROW("OBJECT_SET unknown object id");
    }
    obj->set(*keyStr, std::move(value));
    pushStack(object); // Return the object for chaining
    break;
  }

  // Object intrinsics (VM-level operations)
  case OpCode::OBJECT_KEYS: {
    Value object = popStack();
    if (!object.isObjectId()) {
      COMPILER_THROW("OBJECT_KEYS expects object");
    }
    auto *obj = heap_.object(object.asObjectId());
    if (!obj) {
      COMPILER_THROW("OBJECT_KEYS unknown object id");
    }
    auto arrRef = heap_.allocateArray();
    auto *arr = heap_.array(arrRef.id);
    auto keys = obj->getKeys();
    for (const auto &key : keys) {
      // TODO: string pool registration
      arr->push_back(Value::makeNull());
    }
    pushStack(Value::makeArrayId(arrRef.id));
    break;
  }

  case OpCode::OBJECT_VALUES: {
    Value object = popStack();
    if (!object.isObjectId()) {
      COMPILER_THROW("OBJECT_VALUES expects object");
    }
    auto *obj = heap_.object(object.asObjectId());
    if (!obj) {
      COMPILER_THROW("OBJECT_VALUES unknown object id");
    }
    auto arrRef = heap_.allocateArray();
    auto *arr = heap_.array(arrRef.id);
    auto keys = obj->getKeys();
    for (const auto &key : keys) {
      auto *val = obj->get(key);
      if (val) {
        arr->push_back(*val);
      }
    }
    pushStack(Value::makeArrayId(arrRef.id));
    break;
  }

  case OpCode::OBJECT_ENTRIES: {
    Value object = popStack();
    if (!object.isObjectId()) {
      COMPILER_THROW("OBJECT_ENTRIES expects object");
    }
    auto *obj = heap_.object(object.asObjectId());
    if (!obj) {
      COMPILER_THROW("OBJECT_ENTRIES unknown object id");
    }
    auto arrRef = heap_.allocateArray();
    auto keys = obj->getKeys();
    for (const auto &key : keys) {
      auto *val = obj->get(key);
      if (val) {
        // Create [key, value] tuple as array
        auto tupleRef = heap_.allocateArray();
        auto *tuple = heap_.array(tupleRef.id);
        // TODO: string pool registration
        tuple->push_back(Value::makeNull());
        tuple->push_back(*val);
        auto *arr = heap_.array(arrRef.id);
        arr->push_back(Value::makeArrayId(tupleRef.id));
      }
    }
    pushStack(Value::makeArrayId(arrRef.id));
    break;
  }

  case OpCode::OBJECT_HAS: {
    Value keyValue = popStack();
    Value object = popStack();
    if (!object.isObjectId()) {
      COMPILER_THROW("OBJECT_HAS expects object");
    }
    auto key = keyFromValue(keyValue, &heap_, current_chunk);
    if (!key) {
      COMPILER_THROW("OBJECT_HAS expects string/number/bool key");
    }
    auto *obj = heap_.object(object.asObjectId());
    if (!obj) {
      pushStack(Value::makeBool(false));
    } else {
      pushStack(Value::makeBool(obj->get(*key) != nullptr));
    }
    break;
  }

  case OpCode::OBJECT_DELETE: {
    Value keyValue = popStack();
    Value object = popStack();
    if (!object.isObjectId()) {
      COMPILER_THROW("OBJECT_DELETE expects object");
    }
    auto key = keyFromValue(keyValue, &heap_, current_chunk);
    if (!key) {
      COMPILER_THROW("OBJECT_DELETE expects string/number/bool key");
    }
    auto *obj = heap_.object(object.asObjectId());
    if (!obj) {
      pushStack(Value::makeBool(false));
    } else {
      pushStack(Value::makeBool(obj->data.erase(*key) > 0));
    }
    break;
  }

  // Array intrinsics (VM-level operations)
  case OpCode::ARRAY_POP: {
    Value array = popStack();
    if (!array.isArrayId()) {
      COMPILER_THROW("ARRAY_POP expects array");
    }
    auto *arr = heap_.array(array.asArrayId());
    if (!arr || arr->empty()) {
      pushStack(Value::makeNull());
    } else {
      pushStack(arr->back());
      arr->pop_back();
    }
    break;
  }

  case OpCode::ARRAY_HAS: {
    Value value = popStack();
    Value array = popStack();
    if (!array.isArrayId()) {
      COMPILER_THROW("ARRAY_HAS expects array");
    }
    auto *arr = heap_.array(array.asArrayId());
    if (!arr) {
      pushStack(Value::makeBool(false));
    } else {
      bool found = false;
      for (const auto &elem : *arr) {
        if (valuesEqual(elem, value)) {
          found = true;
          break;
        }
      }
      pushStack(Value::makeBool(found));
    }
    break;
  }

  case OpCode::ARRAY_FIND: {
    Value value = popStack();
    Value array = popStack();
    if (!array.isArrayId()) {
      COMPILER_THROW("ARRAY_FIND expects array");
    }
    auto *arr = heap_.array(array.asArrayId());
    if (!arr) {
      pushStack(Value::makeInt(-1));
    } else {
      int64_t foundIdx = -1;
      for (size_t i = 0; i < arr->size(); i++) {
        if (valuesEqual((*arr)[i], value)) {
          foundIdx = static_cast<int64_t>(i);
          break;
        }
      }
      pushStack(Value::makeInt(foundIdx));
    }
    break;
  }

  // Array higher-order functions (VM intrinsics)
  case OpCode::ARRAY_MAP: {
    Value fn = popStack();
    Value array = popStack();
    if (!array.isArrayId()) {
      COMPILER_THROW("ARRAY_MAP expects array");
    }
    auto *arr = heap_.array(array.asArrayId());
    if (!arr) {
      pushStack(Value::makeNull());
      break;
    }
    if (!fn.isFunctionObjId() && !fn.isClosureId()) {
      COMPILER_THROW("ARRAY_MAP expects function/closure");
    }

    auto resultRef = heap_.allocateArray();
    auto *result = heap_.array(resultRef.id);
    uint64_t resultRootId = pinExternalRoot(Value::makeArrayId(resultRef.id));

    for (size_t i = 0; i < arr->size(); i++) {
      Value mapped = callFunctionSync(fn, {(*arr)[i]});
      result->push_back(mapped);
    }

    unpinExternalRoot(resultRootId);
    pushStack(Value::makeArrayId(resultRef.id));
    break;
  }

  case OpCode::ARRAY_FILTER: {
    Value fn = popStack();
    Value array = popStack();
    if (!array.isArrayId()) {
      COMPILER_THROW("ARRAY_FILTER expects array");
    }
    auto *arr = heap_.array(array.asArrayId());
    if (!arr) {
      pushStack(Value::makeNull());
      break;
    }

    auto resultRef = heap_.allocateArray();
    auto *result = heap_.array(resultRef.id);
    uint64_t resultRootId = pinExternalRoot(Value::makeArrayId(resultRef.id));

    for (size_t i = 0; i < arr->size(); i++) {
      Value predResult = callFunctionSync(fn, {(*arr)[i]});
      if (predResult.isBool() && predResult.asBool()) {
        result->push_back((*arr)[i]);
      }
    }

    unpinExternalRoot(resultRootId);
    pushStack(Value::makeArrayId(resultRef.id));
    break;
  }

  case OpCode::ARRAY_REDUCE: {
    Value initial = popStack();
    Value fn = popStack();
    Value array = popStack();
    if (!array.isArrayId()) {
      COMPILER_THROW("ARRAY_REDUCE expects array");
    }
    auto *arr = heap_.array(array.asArrayId());
    if (!arr) {
      pushStack(initial);
      break;
    }

    Value acc = initial;
    for (size_t i = 0; i < arr->size(); i++) {
      acc = callFunctionSync(fn, {acc, (*arr)[i]});
    }

    pushStack(acc);
    break;
  }

  case OpCode::ARRAY_FOREACH: {
    Value fn = popStack();
    Value array = popStack();
    if (!array.isArrayId()) {
      COMPILER_THROW("ARRAY_FOREACH expects array");
    }
    auto *arr = heap_.array(array.asArrayId());
    if (!arr) {
      pushStack(Value::makeNull());
      break;
    }

    for (size_t i = 0; i < arr->size(); i++) {
      (void)callFunctionSync(fn, {(*arr)[i]});
    }

    pushStack(Value::makeNull());
    break;
  }

  // String intrinsics (VM-level operations)
  case OpCode::STRING_LEN: {
    Value str = popStack();
    pushStack(Value::makeInt(static_cast<int64_t>(toString(str).size())));
    break;
  }

  case OpCode::STRING_UPPER: {
    Value str = popStack();
    std::string s = toString(str);
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    auto ref = createRuntimeString(std::move(s));
    pushStack(Value::makeStringId(ref.id));
    break;
  }

  case OpCode::STRING_LOWER: {
    Value str = popStack();
    std::string s = toString(str);
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    auto ref = createRuntimeString(std::move(s));
    pushStack(Value::makeStringId(ref.id));
    break;
  }

  case OpCode::STRING_TRIM: {
    Value str = popStack();
    std::string s = toString(str);
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) {
      auto ref = createRuntimeString("");
      pushStack(Value::makeStringId(ref.id));
    } else {
      size_t end = s.find_last_not_of(" \t\n\r");
      auto ref = createRuntimeString(s.substr(start, end - start + 1));
      pushStack(Value::makeStringId(ref.id));
    }
    break;
  }

  case OpCode::STRING_HAS: {
    Value substr = popStack();
    Value str = popStack();
    if (!str.isStringValId() || !substr.isStringValId()) {
      COMPILER_THROW("STRING_HAS expects strings");
    }
    // TODO: string pool lookup
    const std::string s = "<string:" + std::to_string(str.asStringValId()) + ">";
    const std::string sub = "<string:" + std::to_string(substr.asStringValId()) + ">";
    pushStack(Value::makeBool(s.find(sub) != std::string::npos));
    break;
  }

  case OpCode::STRING_STARTS: {
    Value prefix = popStack();
    Value str = popStack();
    if (!str.isStringValId() || !prefix.isStringValId()) {
      COMPILER_THROW("STRING_STARTS expects strings");
    }
    // TODO: string pool lookup
    const std::string s = "<string:" + std::to_string(str.asStringValId()) + ">";
    const std::string pre = "<string:" + std::to_string(prefix.asStringValId()) + ">";
    pushStack(Value::makeBool(s.size() >= pre.size() &&
                       s.compare(0, pre.size(), pre) == 0));
    break;
  }

  case OpCode::STRING_ENDS: {
    Value suffix = popStack();
    Value str = popStack();
    if (!str.isStringValId() || !suffix.isStringValId()) {
      COMPILER_THROW("STRING_ENDS expects strings");
    }
    // TODO: string pool lookup
    const std::string s = "<string:" + std::to_string(str.asStringValId()) + ">";
    const std::string suf = "<string:" + std::to_string(suffix.asStringValId()) + ">";
    pushStack(Value::makeBool(s.size() >= suf.size() &&
                       s.compare(s.size() - suf.size(), suf.size(), suf) == 0));
    break;
  }

  // Spread operator - spread array elements
  case OpCode::SPREAD: {
    Value value = popStack();
    if (value.isArrayId()) {
      auto arrRef = ArrayRef{value.asArrayId()};
      auto *arr = heap_.array(arrRef.id);
      if (arr) {
        // Push each element individually
        for (auto &elem : *arr) {
          pushStack(elem);
        }
      }
    } else if (value.isStringValId()) {
      // Spread string into characters
      // TODO: string pool lookup
      std::string str = "<string:" + std::to_string(value.asStringValId()) + ">";
      auto arrRef = heap_.allocateArray();
      auto *arr = heap_.array(arrRef.id);
      if (arr) {
        for (char c : str) {
          // TODO: string pool registration
          arr->push_back(Value::makeNull());
        }
        pushStack(Value::makeArrayId(arrRef.id));
      }
    }
    break;
  }

  // Spread in function call
  case OpCode::SPREAD_CALL: {
    // Similar to SPREAD but marks arguments for spread in CALL
    Value value = popStack();
    if (value.isArrayId()) {
      auto arrRef = ArrayRef{value.asArrayId()};
      auto *arr = heap_.array(arrRef.id);
      if (arr) {
        for (auto &elem : *arr) {
          pushStack(elem);
        }
      }
    }
    break;
  }

  // Type conversion - as operator
  case OpCode::AS_TYPE: {
    if (instruction.operands.size() < 1) {
      COMPILER_THROW("AS_TYPE requires type operand");
    }
    const std::string &typeName =
        instruction.operands[0].toString();
    Value value = popStack();

    if (typeName == "int" || typeName == "Int") {
      pushStack(toInt(value));
    } else if (typeName == "float" || typeName == "Float" ||
               typeName == "double" || typeName == "num" || typeName == "Num") {
      pushStack(toFloat(value));
    } else if (typeName == "string" || typeName == "String") {
      // TODO: string pool integration - for now return null
      pushStack(Value::makeNull());
    } else if (typeName == "bool" || typeName == "Bool" ||
               typeName == "boolean") {
      pushStack(toBool(value));
    } else if (typeName == "array" || typeName == "Array") {
      // Convert to array if possible
      if (value.isArrayId()) {
        pushStack(value);
      } else {
        auto arrRef = heap_.allocateArray();
        pushStack(Value::makeArrayId(arrRef.id));
      }
    } else {
      pushStack(value); // Unknown type, return as-is
    }
    break;
  }

  // toInt() builtin
  case OpCode::TO_INT: {
    Value value = popStack();
    pushStack(toInt(value));
    break;
  }

  // toFloat() builtin
  case OpCode::TO_FLOAT: {
    Value value = popStack();
    pushStack(toFloat(value));
    break;
  }

  // toString() builtin
  case OpCode::TO_STRING: {
    Value value = popStack();
    auto str_ref = createRuntimeString(toString(value));
    pushStack(Value::makeStringId(str_ref.id));
    break;
  }

  // String concatenation
  case OpCode::STRING_CONCAT: {
    Value right = popStack();
    Value left = popStack();
    auto str_ref = createRuntimeString(toString(left) + toString(right));
    pushStack(Value::makeStringId(str_ref.id));
    break;
  }

  // toBool() builtin
  case OpCode::TO_BOOL: {
    Value value = popStack();
    pushStack(toBool(value));
    break;
  }

  // typeof() builtin
  case OpCode::TYPE_OF: {
    Value value = popStack();
    // TODO: string pool integration - return string ID instead of std::string
    // For now, return a placeholder integer
    pushStack(Value::makeInt(0));
    break;
  }

  case OpCode::PRINT: {
    Value value = popStack();
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
    COMPILER_THROW(
        "Unknown opcode: " +
        std::to_string(static_cast<int>(instruction.opcode)));
  }
}

// ============================================================================
// Callback System - VM owns closures, systems use opaque IDs
// ============================================================================

CallbackId VM::registerCallback(const Value &closure) {
  // Accept both ClosureRef and FunctionObject
  if (!closure.isClosureId() && !closure.isFunctionObjId()) {
    COMPILER_THROW("registerCallback expects a closure or function");
  }

  // Pin the closure as an external root (GC will not collect it)
  CallbackId id = static_cast<CallbackId>(pinExternalRoot(closure));

  if (id == INVALID_CALLBACK_ID) {
    COMPILER_THROW("Failed to register callback - invalid ID");
  }

  return id;
}

Value VM::invokeCallback(CallbackId id,
                                 const std::vector<Value> &args) {
  if (id == INVALID_CALLBACK_ID) {
    COMPILER_THROW("invokeCallback called with invalid callback ID");
  }

  // Get the closure from external roots
  auto closureValue = externalRootValue(id);
  if (!closureValue.has_value()) {
    COMPILER_THROW(
        "invokeCallback: callback not found (may have been released)");
  }

  // Call the closure and return result
  return call(*closureValue,
              std::vector<Value>(args.begin(), args.end()));
}

void VM::releaseCallback(CallbackId id) {
  if (id == INVALID_CALLBACK_ID) {
    return; // Nothing to release
  }

  // Unpin the external root (GC can now collect it)
  unpinExternalRoot(id);
}

bool VM::isValidCallback(CallbackId id) const {
  if (id == INVALID_CALLBACK_ID) {
    return false;
  }

  return externalRootValue(id).has_value();
}

// ============================================================================
// Image helpers - GC-managed image creation
// ============================================================================

VMImage VM::createImage(int width, int height, int stride, PixelFormat format,
                        const uint8_t *data) {
  VMImage img;
  img.width = width;
  img.height = height;
  img.stride = stride;
  img.format = format;

  // Create GC-managed byte array for image data
  size_t dataSize =
      stride > 0 ? static_cast<size_t>(stride) * height : width * height * 4;
  auto arrayRef = createHostArray();
  for (size_t i = 0; i < dataSize; ++i) {
    pushHostArrayValue(arrayRef, static_cast<int64_t>(data[i]));
  }

  // Store array reference in VMImage
  img.object_ref = ObjectRef{arrayRef.id};

  return img;
}

VMImage VM::createImageFromRGBA(int width, int height,
                                const std::vector<uint8_t> &rgbaData) {
  // RGBA format: 4 bytes per pixel
  int stride = width * 4;
  return createImage(width, height, stride, PixelFormat::RGBA8,
                     rgbaData.data());
}

std::unique_ptr<BytecodeInterpreter> createVM() {
  return std::make_unique<VM>();
}

// Value utility functions
bool VM::isNull(const Value &value) const {
  return value.isNull();
}

bool VM::isTruthy(const Value &value) {
  // Step 1: null is always falsy
  if (value.isNull()) {
    return false;
  }

  // Step 2: boolean values follow their own truthiness
  if (value.isBool()) {
    return value.asBool();
  }

  // Step 3: numeric values: 0 is falsy, non-zero is truthy
  if (value.isInt()) {
    return value.asInt() != 0;
  }
  if (value.isDouble()) {
    return value.asDouble() != 0.0;
  }

  // Step 4: empty string is falsy, non-empty is truthy
  if (value.isStringValId()) {
    // TODO: string pool lookup - assume truthy for now
    return true;
  }

  // Step 5: arrays are truthy if non-empty
  if (value.isArrayId()) {
    auto *array = heap_.array(value.asArrayId());
    return array && !array->empty();
  }

  // Step 6: objects are always truthy (even if empty)
  if (value.isObjectId()) {
    return true;
  }

  // Step 7: sets are truthy if non-empty
  if (value.isSetId()) {
    // Note: Sets aren't fully implemented yet, but assume truthy if they exist
    return true;
  }

  // Step 8: functions are always truthy
  if (value.isFunctionObjId() || value.isHostFuncId() || value.isClosureId()) {
    return true;
  }

  // Default: should not reach here, but be conservative
  return false;
}

// Duration parsing utility
std::optional<int64_t> VM::parseDuration(const Value &value) const {
  if (value.isInt()) {
    return value.asInt();
  }

  if (value.isDouble()) {
    return static_cast<int64_t>(value.asDouble());
  }

  if (value.isStringValId() || value.isStringId()) {
    const std::string duration_str = resolveStringKey(value);

    // Plain numeric strings are milliseconds.
    static const std::regex numeric_regex(R"(^\d+(?:\.\d+)?$)");
    if (std::regex_match(duration_str, numeric_regex)) {
      return static_cast<int64_t>(std::stod(duration_str));
    }

    // Parse duration strings like "1s", "500ms", "2.5m", "1h"
    static const std::regex duration_regex(R"(^(\d+(?:\.\d+)?)(ms|s|m|h)$)");
    std::smatch match;

    if (std::regex_match(duration_str, match, duration_regex)) {
      double number = std::stod(match[1].str());
      std::string unit = match[2].str();

      if (unit == "ms") {
        return static_cast<int64_t>(number);
      } else if (unit == "s") {
        return static_cast<int64_t>(number * 1000.0);
      } else if (unit == "m") {
        return static_cast<int64_t>(number * 60.0 * 1000.0);
      } else if (unit == "h") {
        return static_cast<int64_t>(number * 60.0 * 60.0 * 1000.0);
      }
    }
  }

  return std::nullopt;
}

// ============================================================================
// Execution Context System - Isolated execution with shared globals
// ============================================================================

VM::VMExecutionContext VM::createExecutionContext() {
  VMExecutionContext ctx;
  ctx.parent_vm_ = this;
  ctx.current_chunk = current_chunk;
  return ctx;
}

Value
VM::VMExecutionContext::invokeCallback(CallbackId id,
                                       const std::vector<Value> &args) {
  if (!parent_vm_) {
    COMPILER_THROW(
        "VMExecutionContext::invokeCallback called on invalid context");
  }
  if (id == INVALID_CALLBACK_ID) {
    COMPILER_THROW("invokeCallback called with invalid callback ID");
  }

  // Get the closure from parent's external roots
  auto closureValue = parent_vm_->externalRootValue(id);
  if (!closureValue.has_value()) {
    COMPILER_THROW(
        "invokeCallback: callback not found (may have been released)");
  }

  // Clear any previous state
  while (!stack.empty())
    stack.pop();
  locals.clear();
  frame_count_ = 0;
  open_upvalues.clear();
  has_current_exception_ = false;
  current_exception_ = nullptr;

  // Determine function index from closure
  uint32_t function_index = 0;
  uint32_t closure_id = 0;
  if (closureValue->isClosureId()) {
    closure_id = closureValue->asClosureId();
    auto *closure = parent_vm_->heap_.closure(closure_id);
    if (!closure) {
      COMPILER_THROW("Closure not found: " +
                               std::to_string(closure_id));
    }
    function_index = closure->function_index;
  } else if (closureValue->isFunctionObjId()) {
    function_index = closureValue->asFunctionObjId();
  } else {
    COMPILER_THROW("Callback must be a closure or function");
  }

  const auto *func = current_chunk->getFunction(function_index);
  if (!func) {
    COMPILER_THROW("Function index not found: " +
                             std::to_string(function_index));
  }

  // Set up initial frame
  if (frame_arena_.size() <= frame_count_) {
    frame_arena_.push_back(CallFrame{func, 0, 0, closure_id});
  } else {
    frame_arena_[frame_count_] = CallFrame{func, 0, 0, closure_id};
  }
  frame_count_++;

  // Set up arguments
  for (size_t i = 0; i < args.size() && i < func->param_count; ++i) {
    locals[i] = args[i];
  }

  // Execute dispatch loop with isolated state
  const size_t stop_frame_depth = 0;
  while (frame_count_ > stop_frame_depth) {
    size_t active_frame_idx = frame_count_ - 1;
    const auto *function = frame_arena_[active_frame_idx].function;
    uint32_t ip = frame_arena_[active_frame_idx].ip;
    size_t entry_frame_count = frame_count_;

    const auto &instruction = function->instructions[ip];

    try {
      executeInstructionInContext(instruction);
    } catch (const ScriptThrow &thrown) {
      // Handle exception
      has_current_exception_ = true;
      current_exception_ = thrown.value;

      bool handled = false;
      while (frame_count_ > 0) {
        auto &frame = frame_arena_[frame_count_ - 1];
        if (!frame.try_stack.empty()) {
          const auto handler = frame.try_stack.back();
          frame.try_stack.pop_back();

          while (stack.size() > handler.stack_depth) {
            stack.pop();
          }

          frame.ip = handler.catch_ip;
          handled = true;
          break;
        }

        auto finished = frame;
        frame_count_--;

        for (auto it = open_upvalues.begin(); it != open_upvalues.end();) {
          if (it->first >= finished.locals_base && it->first < locals.size()) {
            it->second->closed_value = locals[it->first];
            it->second->is_open = false;
            it = open_upvalues.erase(it);
          } else {
            ++it;
          }
        }

        if (locals.size() >= finished.locals_base) {
          locals.resize(finished.locals_base);
        }
      }

      if (!handled) {
        COMPILER_THROW("Uncaught exception: " +
                                 parent_vm_->toString(thrown.value));
      }
      continue;
    }

    if (frame_count_ > stop_frame_depth) {
      active_frame_idx = frame_count_ - 1;
      if (frame_count_ == entry_frame_count &&
          frame_arena_[active_frame_idx].ip == ip) {
        frame_arena_[active_frame_idx].ip++;
      }
    }
  }

  if (stack.empty()) {
    return nullptr;
  }
  Value result = stack.top();
  stack.pop();
  return result;
}

void VM::VMExecutionContext::executeInstructionInContext(
    const Instruction &instruction) {
  // Helper lambdas that operate on THIS context's state (not parent VM)
  auto pop = [this]() -> Value {
    if (stack.empty()) {
      COMPILER_THROW("Stack underflow");
    }
    Value value = stack.top();
    stack.pop();
    return value;
  };

  auto push = [this](Value value) { stack.push(std::move(value)); };

  auto toAbsoluteLocal = [this](uint32_t local_index) -> uint32_t {
    return static_cast<uint32_t>(frame_arena_[frame_count_ - 1].locals_base +
                                 local_index);
  };

  auto ensureLocalIndex = [this](uint32_t absolute_index) {
    if (absolute_index >= locals.size()) {
      locals.resize(static_cast<size_t>(absolute_index) + 1, nullptr);
    }
  };

  auto currentFrame = [this]() -> CallFrame & {
    if (frame_count_ == 0) {
      COMPILER_THROW("No active call frame");
    }
    return frame_arena_[frame_count_ - 1];
  };

  auto getConstant = [this](uint32_t index) -> Value {
    return frame_arena_[frame_count_ - 1].function->constants[index];
  };

  auto doReturn = [this, &pop, &push]() {
    if (frame_count_ == 0) {
      return;
    }

    Value ret = nullptr;
    if (!stack.empty()) {
      ret = pop();
    }

    auto finished = frame_arena_[frame_count_ - 1];
    frame_count_--;

    // Close upvalues
    for (auto it = open_upvalues.begin(); it != open_upvalues.end();) {
      if (it->first >= finished.locals_base && it->first < locals.size()) {
        it->second->closed_value = locals[it->first];
        it->second->is_open = false;
        it = open_upvalues.erase(it);
      } else {
        ++it;
      }
    }

    if (locals.size() >= finished.locals_base) {
      locals.resize(finished.locals_base);
    }

    push(ret);
  };

  switch (instruction.opcode) {
  case OpCode::LOAD_CONST: {
    uint32_t const_index = instruction.operands[0].asInt();
    push(getConstant(const_index));
    break;
  }

  case OpCode::LOAD_GLOBAL: {
    // Get the string from the function's string table
    uint32_t strIndex = instruction.operands[0].asStringValId();
    const auto* func = frame_count_ > 0 ? frame_arena_[frame_count_ - 1].function : nullptr;
    std::string name;
    if (current_chunk) {
      name = current_chunk->getString(strIndex);
    } else {
      name = "<unknown:" + std::to_string(strIndex) + ">";
    }
    // Thread-safe access to parent's globals
    auto value = parent_vm_->getGlobalThreadSafe(name);
    push(value.value_or(nullptr));
    break;
  }

  case OpCode::STORE_GLOBAL: {
    // Get the string from the function's string table
    uint32_t strIndex = instruction.operands[0].asStringValId();
    const auto* func = frame_count_ > 0 ? frame_arena_[frame_count_ - 1].function : nullptr;
    std::string name;
    if (current_chunk) {
      name = current_chunk->getString(strIndex);
    } else {
      name = "<unknown:" + std::to_string(strIndex) + ">";
    }
    Value value = pop();
    parent_vm_->setGlobalThreadSafe(name, std::move(value));
    break;
  }

  case OpCode::LOAD_VAR: {
    uint32_t var_index = instruction.operands[0].asInt();
    uint32_t abs = toAbsoluteLocal(var_index);
    ensureLocalIndex(abs);
    push(locals[abs]);
    break;
  }

  case OpCode::STORE_VAR: {
    uint32_t var_index = instruction.operands[0].asInt();
    uint32_t abs = toAbsoluteLocal(var_index);
    ensureLocalIndex(abs);
    locals[abs] = pop();
    break;
  }

  case OpCode::LOAD_UPVALUE: {
    uint32_t upvalue_index = instruction.operands[0].asInt();
    uint32_t closure_id = currentFrame().closure_id;
    auto *closure = parent_vm_->heap_.closure(closure_id);
    if (!closure || upvalue_index >= closure->upvalues.size() ||
        !closure->upvalues[upvalue_index]) {
      COMPILER_THROW("LOAD_UPVALUE error");
    }
    const auto &cell = closure->upvalues[upvalue_index];
    if (cell->is_open) {
      ensureLocalIndex(cell->open_index);
      push(locals[cell->open_index]);
    } else {
      push(cell->closed_value);
    }
    break;
  }

  case OpCode::STORE_UPVALUE: {
    uint32_t upvalue_index = instruction.operands[0].asInt();
    uint32_t closure_id = currentFrame().closure_id;
    auto *closure = parent_vm_->heap_.closure(closure_id);
    if (!closure || upvalue_index >= closure->upvalues.size() ||
        !closure->upvalues[upvalue_index]) {
      COMPILER_THROW("STORE_UPVALUE error");
    }
    auto &cell = closure->upvalues[upvalue_index];
    Value value = pop();
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
    Value value = pop();
    push(value);
    push(value);
    break;
  }

  case OpCode::PUSH_NULL: {
    push(nullptr);
    break;
  }

  case OpCode::ADD: {
    Value right = pop();
    Value left = pop();
    if (left.isInt() && right.isInt()) {
      push(left.asInt() + right.asInt());
    } else if ((left.isInt() || left.isDouble()) &&
               (right.isInt() || right.isDouble())) {
      double l = left.isInt()
                     ? static_cast<double>(left.asInt())
                     : left.asDouble();
      double r = right.isInt()
                     ? static_cast<double>(right.asInt())
                     : right.asDouble();
      push(l + r);
    } else if (left.isStringValId() && right.isStringValId()) {
      // TODO: string pool lookup
      std::string l = "<string:" + std::to_string(left.asStringValId()) + ">";
      std::string r = "<string:" + std::to_string(right.asStringValId()) + ">";
      // TODO: string pool integration for concatenation
      (void)l;
      (void)r;
      push(Value::makeNull());
    } else {
      COMPILER_THROW("Type mismatch in ADD");
    }
    break;
  }

  case OpCode::SUB: {
    Value right = pop();
    Value left = pop();
    if (left.isInt() && right.isInt()) {
      push(left.asInt() - right.asInt());
    } else {
      double l = left.isInt()
                     ? static_cast<double>(left.asInt())
                     : left.asDouble();
      double r = right.isInt()
                     ? static_cast<double>(right.asInt())
                     : right.asDouble();
      push(l - r);
    }
    break;
  }

  case OpCode::MUL: {
    Value right = pop();
    Value left = pop();
    if (left.isInt() && right.isInt()) {
      push(left.asInt() * right.asInt());
    } else {
      double l = left.isInt()
                     ? static_cast<double>(left.asInt())
                     : left.asDouble();
      double r = right.isInt()
                     ? static_cast<double>(right.asInt())
                     : right.asDouble();
      push(l * r);
    }
    break;
  }

  case OpCode::DIV: {
    Value right = pop();
    Value left = pop();
    double l = left.isInt()
                   ? static_cast<double>(left.asInt())
                   : left.asDouble();
    double r = right.isInt()
                   ? static_cast<double>(right.asInt())
                   : right.asDouble();
    if (r == 0)
      throw ScriptThrow{Value("Division by zero")};
    push(l / r);
    break;
  }

  case OpCode::EQ: {
    Value right = pop();
    Value left = pop();
    if (left.isInt() && right.isInt()) {
      push(left.asInt() == right.asInt());
    } else if (left.isStringValId() && right.isStringValId()) {
      // TODO: string pool lookup
      std::string l = "<string:" + std::to_string(left.asStringValId()) + ">";
      std::string r = "<string:" + std::to_string(right.asStringValId()) + ">";
      push(l == r);
    } else {
      // For other types, only equal if same type and both are null
      if (left.isNull() && right.isNull()) {
        push(true);
      } else {
        push(false);
      }
    }
    break;
  }

  case OpCode::LT: {
    Value right = pop();
    Value left = pop();
    if (left.isInt() && right.isInt()) {
      push(left.asInt() < right.asInt());
    } else {
      double l = left.isInt()
                     ? static_cast<double>(left.asInt())
                     : left.asDouble();
      double r = right.isInt()
                     ? static_cast<double>(right.asInt())
                     : right.asDouble();
      push(l < r);
    }
    break;
  }

  case OpCode::GT: {
    Value right = pop();
    Value left = pop();
    if (left.isInt() && right.isInt()) {
      push(left.asInt() > right.asInt());
    } else {
      double l = left.isInt()
                     ? static_cast<double>(left.asInt())
                     : left.asDouble();
      double r = right.isInt()
                     ? static_cast<double>(right.asInt())
                     : right.asDouble();
      push(l > r);
    }
    break;
  }

  case OpCode::JUMP: {
    uint32_t target = instruction.operands[0].asInt();
    currentFrame().ip = target;
    break;
  }

  case OpCode::JUMP_IF_FALSE: {
    uint32_t target = instruction.operands[0].asInt();
    Value condition = pop();
    if (!parent_vm_->isTruthy(condition)) {
      currentFrame().ip = target;
    }
    break;
  }

  case OpCode::CALL: {
    uint32_t arg_count = instruction.operands[0].asInt();
    std::vector<Value> args(arg_count);
    for (uint32_t i = 0; i < arg_count; ++i) {
      args[arg_count - 1 - i] = pop();
    }
    Value callee_value = pop();

    // Handle host function call
    if (callee_value.isHostFuncId()) {
      // TODO: host func name lookup
      (void)callee_value.asHostFuncId();
      COMPILER_THROW("Host function call in execution context not yet supported with NaN boxing");
    }

    // Handle VM function/closure call
    uint32_t function_index = 0;
    uint32_t new_closure_id = 0;
    if (callee_value.isFunctionObjId()) {
      function_index = callee_value.asFunctionObjId();
    } else if (callee_value.isClosureId()) {
      new_closure_id = callee_value.asClosureId();
      auto *closure = parent_vm_->heap_.closure(new_closure_id);
      if (!closure) {
        COMPILER_THROW("Closure not found");
      }
      function_index = closure->function_index;
    } else {
      COMPILER_THROW("CALL expects function or closure");
    }

    const auto *callee = current_chunk->getFunction(function_index);
    if (!callee) {
      COMPILER_THROW("Function not found");
    }

    // Advance caller IP
    currentFrame().ip++;

    size_t base = locals.size();
    locals.resize(base + callee->local_count, nullptr);
    if (frame_arena_.size() <= frame_count_) {
      frame_arena_.push_back(CallFrame{callee, 0, base, new_closure_id});
    } else {
      frame_arena_[frame_count_] = CallFrame{callee, 0, base, new_closure_id};
    }
    frame_count_++;

    // Set up parameters
    for (uint32_t i = 0; i < callee->param_count && i < args.size(); ++i) {
      locals[base + i] = std::move(args[i]);
    }
    break;
  }

  case OpCode::RETURN: {
    doReturn();
    break;
  }

  case OpCode::CLOSURE: {
    uint32_t function_index = instruction.operands[0].asInt();
    const auto *target = current_chunk->getFunction(function_index);
    if (!target) {
      COMPILER_THROW("CLOSURE references unknown function index");
    }

    // Capture upvalues
    std::vector<std::shared_ptr<GCHeap::UpvalueCell>> upvalues;
    upvalues.reserve(target->upvalues.size());
    for (const auto &descriptor : target->upvalues) {
      if (descriptor.captures_local) {
        uint32_t abs = toAbsoluteLocal(descriptor.index);
        ensureLocalIndex(abs);
        auto open_it = open_upvalues.find(abs);
        if (open_it == open_upvalues.end()) {
          auto cell = std::make_shared<GCHeap::UpvalueCell>();
          cell->is_open = true;
          cell->open_index = abs;
          open_upvalues.emplace(abs, cell);
          upvalues.push_back(std::move(cell));
        } else {
          upvalues.push_back(open_it->second);
        }
      } else {
        uint32_t parent_closure_id = currentFrame().closure_id;
        if (parent_closure_id != 0) {
          auto *parent_closure = parent_vm_->heap_.closure(parent_closure_id);
          if (parent_closure &&
              descriptor.index < parent_closure->upvalues.size()) {
            upvalues.push_back(parent_closure->upvalues[descriptor.index]);
          }
        }
      }
    }

    push(Value::makeClosureId(parent_vm_->heap_.allocateClosure(GCHeap::RuntimeClosure{
        .function_index = function_index, .upvalues = std::move(upvalues)}).id));
    break;
  }

  case OpCode::CALL_HOST: {
    // Get the function name from the function's string table
    uint32_t strIndex = instruction.operands[0].asStringValId();
    const auto* func = frame_count_ > 0 ? frame_arena_[frame_count_ - 1].function : nullptr;
    std::string function_name;
    if (current_chunk) {
      function_name = current_chunk->getString(strIndex);
    } else {
      function_name = "<unknown:" + std::to_string(strIndex) + ">";
    }
    uint32_t arg_count = static_cast<uint32_t>(instruction.operands[1].asInt());

    std::vector<Value> args(arg_count);
    for (uint32_t i = 0; i < arg_count; ++i) {
      args[arg_count - 1 - i] = pop();
    }

    auto it = parent_vm_->host_functions.find(function_name);
    if (it != parent_vm_->host_functions.end()) {
      push(it->second(args));
    } else {
      COMPILER_THROW(parent_vm_->formatErrorWithContext(
          "Host function not found: " + function_name));
    }
    break;
  }

  default:
    COMPILER_THROW(parent_vm_->formatErrorWithContext(
        "Unsupported opcode in execution context: " +
        std::to_string(static_cast<int>(instruction.opcode))));
  }
}

void VM::setGlobalThreadSafe(const std::string &name, Value value) {
  std::unique_lock<std::shared_mutex> lock(globals_mutex_);
  globals[name] = std::move(value);
}

std::optional<Value>
VM::getGlobalThreadSafe(const std::string &name) const {
  std::shared_lock<std::shared_mutex> lock(globals_mutex_);
  auto it = globals.find(name);
  if (it == globals.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::string VM::resolveStringKey(const Value &value) const {
  if (value.isStringId()) {
    if (auto *s = heap_.string(value.asStringId())) {
      return *s;
    }
    return "<string:" + std::to_string(value.asStringId()) + ">";
  }
  if (value.isStringValId()) {
    if (current_chunk) {
      return current_chunk->getString(value.asStringValId());
    }
    return "<string:" + std::to_string(value.asStringValId()) + ">";
  }
  if (value.isInt()) return std::to_string(value.asInt());
  if (value.isDouble()) {
    std::ostringstream out;
    out << value.asDouble();
    return out.str();
  }
  if (value.isBool()) return value.asBool() ? "true" : "false";
  return value.toString();
}

} // namespace havel::compiler
