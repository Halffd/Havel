#include "VM.hpp"

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

namespace havel::compiler {

namespace {
// Helper function to compare two BytecodeValues for equality
static bool valuesEqual(const BytecodeValue &a, const BytecodeValue &b) {
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
  BytecodeValue value;
};

// Internal toString with depth limit only (no cycle detection - confuses users)
std::string toStringInternal(const BytecodeValue &value, GCHeap *heap,
                             std::unordered_set<uint32_t> &visitedIds,
                             int depth);

std::string toStringInternal(const BytecodeValue &value, GCHeap *heap,
                             std::unordered_set<uint32_t> &visitedIds,
                             int depth) {
  // Depth limit to prevent stack overflow
  if (depth > 8) {
    return "...";
  }

  if (value.isNull()) {
    return "null";
  }
  if (value.isBool()) {
    return value.asBool() ? "true" : "false";
  }
  if (value.isInt()) {
    return std::to_string(value.asInt());
  }
  if (value.isDouble()) {
    std::ostringstream out;
    out << value.asDouble();
    return out.str();
  }
  if (value.isStringValId()) {
    // TODO: string pool lookup - for now return placeholder
    return "<string:" + std::to_string(value.asStringValId()) + ">";
  }
  if (value.isFunctionObjId()) {
    return "fn[" + std::to_string(value.asFunctionObjId()) + "]";
  }
  if (value.isClosureId()) {
    return "closure[" + std::to_string(value.asClosureId()) + "]";
  }
  if (value.isArrayId()) {
    if (!heap) {
      return "array[" + std::to_string(value.asArrayId()) + "]";
    }
    auto *arr = heap->array(value.asArrayId());
    if (!arr) {
      return "array[]";
    }

    std::string result = "[";
    for (size_t i = 0; i < arr->size(); ++i) {
      if (i > 0)
        result += ", ";
      result += toStringInternal((*arr)[i], heap, visitedIds, depth + 1);
    }
    result += "]";
    return result;
  }
  if (value.isObjectId()) {
    if (!heap) {
      return "object[" + std::to_string(value.asObjectId()) + "]";
    }
    auto *obj = heap->object(value.asObjectId());
    if (!obj) {
      return "object{}";
    }

    std::string result = "{";
    bool first = true;
    for (const auto &[key, val] : *obj) {
      if (!first)
        result += ", ";
      first = false;
      result += "\"" + key +
                "\": " + toStringInternal(val, heap, visitedIds, depth + 1);
    }
    result += "}";
    return result;
  }
  if (value.isSetId()) {
    return "set[" + std::to_string(value.asSetId()) + "]";
  }
  if (value.isHostFuncId()) {
    // TODO: host func name lookup - for now return placeholder
    return "hostfn[" + std::to_string(value.asHostFuncId()) + "]";
  }
  return "unknown";
}

// Wrapper without visited set (for backward compatibility)
std::string toStringInternal(const BytecodeValue &value, GCHeap *heap) {
  std::unordered_set<uint32_t> visitedIds;
  return toStringInternal(value, heap, visitedIds, 0);
}
} // anonymous namespace

// Public toString without heap (for backward compatibility)
std::string toString(const BytecodeValue &value) {
  return toStringInternal(value, nullptr);
}

// Public toString with heap (for formatted output)
std::string toString(const BytecodeValue &value, GCHeap *heap) {
  return toStringInternal(value, heap);
}

// Type conversion helpers
int64_t toInt(const BytecodeValue &value) {
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
    // TODO: string pool lookup
    try {
      return std::stoll("<string:" + std::to_string(value.asStringValId()) + ">");
    } catch (...) {
      return 0;
    }
  }
  return 0;
}

double toFloat(const BytecodeValue &value) {
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
    // TODO: string pool lookup
    try {
      return std::stod("<string:" + std::to_string(value.asStringValId()) + ">");
    } catch (...) {
      return 0.0;
    }
  }
  return 0.0;
}

bool toBool(const BytecodeValue &value) {
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
    // TODO: string pool lookup - non-null string id is truthy
    return true;
  }
  // Collections: JavaScript truthiness (all collections are truthy, even empty)
  // This matches: if (arr) { ... } pattern
  if (value.isArrayId()) {
    return true;
  }
  if (value.isObjectId()) {
    return true;
  }
  if (value.isSetId()) {
    return true;
  }
  return false; // null, undefined, etc.
}

std::optional<int64_t> indexFromValue(const BytecodeValue &value) {
  if (value.isInt()) {
    return value.asInt();
  }
  if (value.isDouble()) {
    return static_cast<int64_t>(value.asDouble());
  }
  return std::nullopt;
}

std::optional<std::string> keyFromValue(const BytecodeValue &value) {
  if (value.isStringValId()) {
    // TODO: string pool lookup
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

// Rust-style error formatting with source line and arrow
std::string VM::formatErrorWithContext(const std::string &message) const {
  if (frame_count_ == 0 || !frame_arena_[frame_count_ - 1].function) {
    return message + " (at <unknown>)";
  }

  const auto &frame = frame_arena_[frame_count_ - 1];
  const auto *function = frame.function;

  if (frame.ip >= function->instruction_locations.size()) {
    return message + " (at <unknown>)";
  }

  const auto &loc = function->instruction_locations[frame.ip];
  if (loc.line == 0) {
    return message + " (at <unknown>)";
  }

  std::string result = message + "\n";
  result += "  --> " + loc.filename + ":" + std::to_string(loc.line) + ":" +
            std::to_string(loc.column) + "\n";

  // Try to read the source line from file
  if (!loc.filename.empty()) {
    std::ifstream file(loc.filename);
    if (file.is_open()) {
      std::string line;
      uint32_t current_line = 1;
      while (std::getline(file, line)) {
        if (current_line == loc.line) {
          // Show the source line
          result += "   |\n";
          result += "   | " + line + "\n";

          // Create arrow pointing to column
          std::string arrow = "   | ";
          // Calculate position: clamp column to line length
          size_t arrow_pos = static_cast<size_t>(loc.column > 1 ? loc.column - 1 : 0);
          if (arrow_pos > line.length()) {
            arrow_pos = line.length();
          }
          for (size_t i = 0; i < arrow_pos; i++) {
            arrow += " ";
          }
          arrow += "^";
          result += arrow + "\n";
          break;
        }
        current_line++;
      }
    }
  }

  return result;
}

VM::VM() { registerDefaultHostFunctions(); }

VM::VM(const havel::HostContext &ctx) {
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

template <typename T> T VM::getValue(const BytecodeValue &value) {
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

  throw std::runtime_error("Invalid type conversion");
}

const VM::CallFrame &VM::currentFrame() const {
  if (frame_count_ == 0) {
    throw std::runtime_error("No active call frame");
  }
  return frame_arena_[frame_count_ - 1];
}

VM::CallFrame &VM::currentFrame() {
  if (frame_count_ == 0) {
    throw std::runtime_error("No active call frame");
  }
  return frame_arena_[frame_count_ - 1];
}

BytecodeValue VM::getConstant(uint32_t index) {
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

void VM::scheduleCall(const BytecodeValue &fn,
                      const std::vector<BytecodeValue> &args,
                      BytecodeValue &result, bool &completed) {
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
BytecodeValue VM::callFunctionSync(const BytecodeValue &fn,
                                   const std::vector<BytecodeValue> &args) {
  size_t savedStackSize = stack.size();
  size_t savedFrameCount = frame_count_;

  // Execute callback
  doCall(fn, args, false);
  runDispatchLoop(savedFrameCount);

  // Get result from stack top
  BytecodeValue result;
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
  // Also register as a global value so it can be loaded and called like a
  // normal function This enables pipeline syntax: "hello" | upper
  globals[name] = BytecodeValue(HostFunctionRef{name});
}

void VM::registerHostFunction(const std::string &name, size_t arity,
                              BytecodeHostFunction function) {
  registerHostFunction(
      name,
      [arity, function = std::move(function),
       name](const std::vector<BytecodeValue> &args) -> BytecodeValue {
        if (args.size() != arity) {
          throw std::runtime_error("Host function '" + name + "' expects " +
                                   std::to_string(arity) + " arguments, got " +
                                   std::to_string(args.size()));
        }
        return function(args);
      });
}

bool VM::hasHostFunction(const std::string &name) const {
  return host_functions.find(name) != host_functions.end();
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

void VM::setHostObjectField(ObjectRef object_ref, const std::string &key,
                            BytecodeValue value) {
  auto *object = heap_.object(object_ref.id);
  if (!object) {
    throw std::runtime_error("setHostObjectField unknown object id");
  }
  (*object)[key] = std::move(value);
}

void VM::pushHostArrayValue(ArrayRef array_ref, BytecodeValue value) {
  auto *array = heap_.array(array_ref.id);
  if (!array) {
    throw std::runtime_error("pushHostArrayValue unknown array id");
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

BytecodeValue VM::getHostArrayValue(ArrayRef array_ref, size_t index) {
  auto *array = heap_.array(array_ref.id);
  if (!array || index >= array->size())
    return BytecodeValue::makeNull();
  return (*array)[index];
}

void VM::setHostArrayValue(ArrayRef array_ref, size_t index,
                           BytecodeValue value) {
  auto *array = heap_.array(array_ref.id);
  if (!array)
    return;
  if (index >= array->size()) {
    // Extend array if needed
    while (array->size() <= index) {
      array->push_back(BytecodeValue::makeNull());
    }
  }
  (*array)[index] = std::move(value);
}

BytecodeValue VM::popHostArrayValue(ArrayRef array_ref) {
  auto *array = heap_.array(array_ref.id);
  if (!array || array->empty())
    return BytecodeValue::makeNull();
  auto value = std::move(array->back());
  array->pop_back();
  return value;
}

void VM::insertHostArrayValue(ArrayRef array_ref, size_t index,
                              BytecodeValue value) {
  auto *array = heap_.array(array_ref.id);
  if (!array)
    return;
  if (index > array->size())
    index = array->size();
  array->insert(array->begin() + index, std::move(value));
}

BytecodeValue VM::removeHostArrayValue(ArrayRef array_ref, size_t index) {
  auto *array = heap_.array(array_ref.id);
  if (!array || index >= array->size())
    return BytecodeValue::makeNull();
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

BytecodeValue VM::getStructField(StructRef struct_ref, size_t index) {
  auto it = heap_.structs_.find(struct_ref.id);
  if (it == heap_.structs_.end() || index >= it->second.size()) {
    return BytecodeValue::makeNull();
  }
  return it->second[index];
}

void VM::setStructField(StructRef struct_ref, size_t index,
                        const BytecodeValue &value) {
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

BytecodeValue VM::getClassField(ClassRef class_ref, size_t index) {
  auto it = heap_.classes_.find(class_ref.id);
  if (it == heap_.classes_.end() || index >= it->second.size()) {
    return BytecodeValue::makeNull();
  }
  return it->second[index];
}

void VM::setClassField(ClassRef class_ref, size_t index,
                       const BytecodeValue &value) {
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

BytecodeValue VM::getEnumPayload(EnumRef enum_ref, size_t index) {
  auto it = heap_.enums_.find(enum_ref.id);
  if (it == heap_.enums_.end() || index >= it->second.second.size()) {
    return BytecodeValue::makeNull();
  }
  return it->second.second[index];
}

void VM::setEnumPayload(EnumRef enum_ref, size_t index,
                        const BytecodeValue &value) {
  auto it = heap_.enums_.find(enum_ref.id);
  if (it == heap_.enums_.end() || index >= it->second.second.size()) {
    return;
  }
  it->second.second[index] = value;
}

// Membership helpers
bool VM::arrayContains(ArrayRef array_ref, const BytecodeValue &value) {
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
IteratorRef VM::createIterator(const BytecodeValue &iterable) {
  IteratorRef ref;
  ref.id = heap_.createIterator(iterable);
  return ref;
}

BytecodeValue VM::iteratorNext(IteratorRef iterRef) {
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

std::vector<std::pair<std::string, BytecodeValue>>
VM::getHostObjectEntries(ObjectRef object_ref) {
  auto *object = heap_.object(object_ref.id);
  if (!object)
    return {};
  return std::vector<std::pair<std::string, BytecodeValue>>(object->begin(),
                                                            object->end());
}

bool VM::hasHostObjectField(ObjectRef object_ref, const std::string &key) {
  auto *object = heap_.object(object_ref.id);
  if (!object)
    return false;
  return object->find(key) != object->end();
}

BytecodeValue VM::getHostObjectField(ObjectRef object_ref,
                                     const std::string &key) {
  auto *object = heap_.object(object_ref.id);
  if (!object)
    return BytecodeValue::makeNull();
  auto it = object->find(key);
  if (it == object->end())
    return BytecodeValue::makeNull();
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
BytecodeValue VM::callHostFunction(const BytecodeValue &fn,
                                   const std::vector<BytecodeValue> &args) {
  if (fn.isHostFuncId()) {
    // TODO: host func name lookup
    // For now, return null since we can't resolve the name without a table
    (void)fn.asHostFuncId();
    return BytecodeValue::makeNull();
  }
  return BytecodeValue::makeNull();
}

// General function call (handles both VM closures and host functions)
BytecodeValue VM::callFunction(const BytecodeValue &fn,
                               const std::vector<BytecodeValue> &args) {
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
VM::getPrototypeMethod(const BytecodeValue &value,
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

std::vector<std::string> VM::getPrototypeMethods(const BytecodeValue &value) {
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

uint64_t VM::pinExternalRoot(const BytecodeValue &value) {
  return heap_.pinExternalRoot(value);
}

bool VM::unpinExternalRoot(uint64_t root_id) {
  return heap_.unpinExternalRoot(root_id);
}

std::optional<BytecodeValue> VM::externalRootValue(uint64_t root_id) const {
  return heap_.externalRoot(root_id);
}

void VM::registerDefaultHostFunctions() {
  // Register print as both host function AND global (for closure access)
  registerHostFunction("print", [this](const std::vector<BytecodeValue> &args) {
    // Check if last arg is kwargs object (has end= or delim=)
    std::string delim = " ";
    std::string end = "\n";
    size_t argCount = args.size();

    // Check for kwargs object as last argument
    if (!args.empty() && args.back().isObjectId()) {
      auto *kwargsObj = heap_.object(args.back().asObjectId());
      if (kwargsObj) {
        auto itEnd = kwargsObj->find("end");
        if (itEnd != kwargsObj->end() &&
            itEnd->second.isStringValId()) {
          // TODO: string pool lookup
          end = "<string:" + std::to_string(itEnd->second.asStringValId()) + ">";
        }
        auto itDelim = kwargsObj->find("delim");
        if (itDelim != kwargsObj->end() &&
            itDelim->second.isStringValId()) {
          // TODO: string pool lookup
          delim = "<string:" + std::to_string(itDelim->second.asStringValId()) + ">";
        }
        argCount--; // Don't count kwargs as a value to print
      }
    }

    // Print values with delimiter
    for (size_t i = 0; i < argCount; ++i) {
      if (i > 0) {
        std::cout << delim;
      }
      // Use heap-aware toString for proper array/object formatting
      std::cout << toString(args[i], &heap_);
    }
    std::cout << end;
    return BytecodeValue::makeNull();
  });

  // fmt(format_string, ...) - Python-style string formatting
  registerHostFunction("fmt", [this](const std::vector<BytecodeValue> &args) {
    if (args.empty()) {
      throw std::runtime_error("fmt() requires at least a format string");
    }

    // Get format string
    if (!args[0].isStringValId()) {
      throw std::runtime_error("fmt() format must be a string");
    }
    // TODO: string pool lookup
    std::string formatStr = "<string:" + std::to_string(args[0].asStringValId()) + ">";

    // Convert args to strings for formatting
    std::vector<std::string> argStrings;
    for (size_t i = 1; i < args.size(); ++i) {
      argStrings.push_back(toString(args[i], &heap_));
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

    return BytecodeValue(result);
  });

  registerHostFunction("clock_ms", 0, [](const std::vector<BytecodeValue> &) {
    const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now())
                         .time_since_epoch()
                         .count();
    return BytecodeValue::makeInt(static_cast<int64_t>(now));
  });

  registerHostFunction(
      "sleep_ms", 1, [](const std::vector<BytecodeValue> &args) {
        if (!args[0].isInt()) {
          throw std::runtime_error(
              "sleep_ms expects exactly 1 integer argument");
        }

        int64_t duration_ms = args[0].asInt();
        if (duration_ms < 0) {
          throw std::runtime_error("sleep_ms duration cannot be negative");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
        return BytecodeValue::makeNull();
      });

  // Enhanced sleep() with duration string support
  registerHostFunction(
      "sleep", 1, [this](const std::vector<BytecodeValue> &args) {
        if (args.empty()) {
          throw std::runtime_error("sleep() requires one argument");
        }

        auto duration_ms = parseDuration(args[0]);
        if (!duration_ms) {
          throw std::runtime_error(
              "sleep(): invalid duration format. Use numbers (ms) or strings "
              "like '1s', '500ms', '2.5m', '1h'");
        }

        if (*duration_ms < 0) {
          throw std::runtime_error("sleep(): duration cannot be negative");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(*duration_ms));
        return BytecodeValue::makeNull();
      });

  // Type conversion builtins
  registerHostFunction("int", 1, [](const std::vector<BytecodeValue> &args) {
    return BytecodeValue(toInt(args[0]));
  });

  registerHostFunction("num", 1, [](const std::vector<BytecodeValue> &args) {
    return BytecodeValue(toFloat(args[0]));
  });

  // Instrumentation: assert(condition, message?)
  registerHostFunction("assert", [](const std::vector<BytecodeValue> &args) {
    if (args.empty()) {
      throw std::runtime_error(
          "assert() requires at least a condition argument");
    }
    bool condition = false;
    if (args[0].isBool()) {
      condition = args[0].asBool();
    } else if (args[0].isInt()) {
      condition = args[0].asInt() != 0;
    }
    if (!condition) {
      std::string msg = "Assertion failed";
      if (args.size() > 1 && args[1].isStringValId()) {
        // TODO: string pool lookup
        msg = "<string:" + std::to_string(args[1].asStringValId()) + ">";
      }
      throw std::runtime_error(msg);
    }
    return BytecodeValue::makeNull();
  });

  // Performance: clock_ns() - high-resolution clock in nanoseconds
  registerHostFunction("clock_ns", 0, [](const std::vector<BytecodeValue> &) {
    const auto now = std::chrono::time_point_cast<std::chrono::nanoseconds>(
                         std::chrono::steady_clock::now())
                         .time_since_epoch()
                         .count();
    return BytecodeValue::makeInt(static_cast<int64_t>(now));
  });

  // Performance: clock_us() - clock in microseconds
  registerHostFunction("clock_us", 0, [](const std::vector<BytecodeValue> &) {
    const auto now = std::chrono::time_point_cast<std::chrono::microseconds>(
                         std::chrono::steady_clock::now())
                         .time_since_epoch()
                         .count();
    return BytecodeValue::makeInt(static_cast<int64_t>(now));
  });

  registerHostFunction("str", 1, [](const std::vector<BytecodeValue> &args) {
    return BytecodeValue(toString(args[0]));
  });

  // Additional type conversion (useful even if not in docs)
  registerHostFunction("bool", 1, [](const std::vector<BytecodeValue> &args) {
    return BytecodeValue(toBool(args[0]));
  });

  registerHostFunction("type", 1, [](const std::vector<BytecodeValue> &args) {
    const auto &value = args[0];
    std::string typeName;
    if (value.isNull()) {
      typeName = "null";
    } else if (value.isBool()) {
      typeName = "bool";
    } else if (value.isInt()) {
      typeName = "int";
    } else if (value.isDouble()) {
      typeName = "float";
    } else if (value.isStringValId()) {
      typeName = "string";
    } else if (value.isArrayId()) {
      typeName = "array";
    } else if (value.isObjectId()) {
      typeName = "object";
    } else if (value.isFunctionObjId()) {
      typeName = "function";
    } else if (value.isClosureId()) {
      typeName = "closure";
    } else if (value.isHostFuncId()) {
      typeName = "function";
    } else {
      typeName = "unknown";
    }
    return BytecodeValue(typeName);
  });

  auto registerSystemGc = [this](const std::string &name) {
    registerHostFunction(name, 0, [this](const std::vector<BytecodeValue> &) {
      runGarbageCollection();
      return BytecodeValue::makeNull();
    });
  };
  registerSystemGc("system.gc");
  registerSystemGc("system_gc");

  auto registerSystemGcStats = [this](const std::string &name) {
    registerHostFunction(name, 0, [this](const std::vector<BytecodeValue> &) {
      const auto stats = gcStats();
      const auto object_ref = createHostObject();
      setHostObjectField(object_ref, "heapSize",
                         static_cast<int64_t>(stats.heap_size));
      setHostObjectField(object_ref, "objectCount",
                         static_cast<int64_t>(stats.object_count));
      setHostObjectField(object_ref, "collections",
                         static_cast<int64_t>(stats.collections));
      setHostObjectField(object_ref, "lastPauseNs",
                         static_cast<int64_t>(stats.last_pause_ns));
      return BytecodeValue(object_ref);
    });
  };
  registerSystemGcStats("system.gcStats");
  registerSystemGcStats("system_gcStats");

  registerHostFunction(
      "struct.define", [this](const std::vector<BytecodeValue> &args) {
        std::cerr << "[DEBUG] struct.define called with " << args.size()
                  << " args\n";
        if (args.size() != 2 || !args[0].isStringValId() ||
            !args[1].isArrayId()) {
          std::cerr << "[DEBUG] struct.define: argument type check failed\n";
          throw std::runtime_error(
              "struct.define(name, fields) expects (string, array)");
        }
        // TODO: string pool lookup
        const auto &name = "<string:" + std::to_string(args[0].asStringValId()) + ">";
        auto *field_array = heap_.array(args[1].asArrayId());
        if (!field_array) {
          std::cerr << "[DEBUG] struct.define: field_array is null\n";
          throw std::runtime_error(
              "struct.define received invalid fields array");
        }
        std::cerr << "[DEBUG] struct.define: field_array size = "
                  << field_array->size() << "\n";
        std::vector<std::string> fields;
        fields.reserve(field_array->size());
        for (const auto &value : *field_array) {
          if (!value.isStringValId()) {
            std::cerr << "[DEBUG] struct.define: field value is not a string\n";
            throw std::runtime_error(
                "struct.define fields must contain only strings");
          }
          // TODO: string pool lookup
          fields.push_back("<string:" + std::to_string(value.asStringValId()) + ">");
        }
        uint32_t type_id = registerStructType(name, fields);
        struct_type_ids_by_name_[name] = type_id;
        std::cerr << "[DEBUG] struct.define: returning type_id = " << type_id
                  << "\n";
        return BytecodeValue::makeInt(static_cast<int64_t>(type_id));
      });

  registerHostFunction(
      "struct.new", [this](const std::vector<BytecodeValue> &args) {
        if (args.empty()) {
          throw std::runtime_error(
              "struct.new(type, ...values) requires a type argument");
        }
        uint32_t type_id = 0;
        if (args[0].isInt()) {
          type_id = static_cast<uint32_t>(args[0].asInt());
        } else if (args[0].isStringValId()) {
          // TODO: string pool lookup
          const auto &name = "<string:" + std::to_string(args[0].asStringValId()) + ">";
          auto it = struct_type_ids_by_name_.find(name);
          if (it == struct_type_ids_by_name_.end()) {
            throw std::runtime_error("Unknown struct type: " + name);
          }
          type_id = it->second;
        } else {
          throw std::runtime_error("struct.new type must be string or int");
        }

        const size_t field_count = heap_.structFieldCount(type_id);
        StructRef ref = createStruct(type_id, field_count);
        const size_t provided = args.size() - 1;
        for (size_t i = 0; i < provided && i < field_count; ++i) {
          setStructField(ref, i, args[i + 1]);
        }
        return BytecodeValue(ref);
      });

  registerHostFunction(
      "struct.get", [this](const std::vector<BytecodeValue> &args) {
        if (args.size() != 2 || !args[0].isStructId()) {
          throw std::runtime_error("struct.get(struct, field) expects struct");
        }
        StructRef ref{args[0].asStructId()};
        size_t index = 0;
        if (args[1].isInt()) {
          index = static_cast<size_t>(args[1].asInt());
        } else if (args[1].isStringValId()) {
          // TODO: string pool lookup
          auto idx = heap_.structFieldIndex(ref.typeId,
                                            "<string:" + std::to_string(args[1].asStringValId()) + ">");
          if (!idx.has_value()) {
            return BytecodeValue::makeNull();
          }
          index = *idx;
        } else {
          throw std::runtime_error("struct.get field must be string or int");
        }
        return getStructField(ref, index);
      });

  registerHostFunction(
      "struct.set", [this](const std::vector<BytecodeValue> &args) {
        if (args.size() != 3 || !args[0].isStructId()) {
          throw std::runtime_error(
              "struct.set(struct, field, value) expects struct");
        }
        StructRef ref{args[0].asStructId()};
        size_t index = 0;
        if (args[1].isInt()) {
          index = static_cast<size_t>(args[1].asInt());
        } else if (args[1].isStringValId()) {
          // TODO: string pool lookup
          auto idx = heap_.structFieldIndex(ref.typeId,
                                            "<string:" + std::to_string(args[1].asStringValId()) + ">");
          if (!idx.has_value()) {
            throw std::runtime_error("Unknown struct field name");
          }
          index = *idx;
        } else {
          throw std::runtime_error("struct.set field must be string or int");
        }
        setStructField(ref, index, args[2]);
        return BytecodeValue(ref);
      });

  // Class operations (reference type)
  registerHostFunction(
      "class.define", [this](const std::vector<BytecodeValue> &args) {
        if (args.size() != 2 || !args[0].isStringValId() ||
            !args[1].isArrayId()) {
          throw std::runtime_error(
              "class.define(name, fields) expects (string, array)");
        }
        // TODO: string pool lookup
        const auto &name = "<string:" + std::to_string(args[0].asStringValId()) + ">";
        auto *field_array = heap_.array(args[1].asArrayId());
        if (!field_array) {
          throw std::runtime_error(
              "class.define received invalid fields array");
        }
        std::vector<std::string> fields;
        fields.reserve(field_array->size());
        for (const auto &value : *field_array) {
          if (!value.isStringValId()) {
            throw std::runtime_error(
                "class.define fields must contain only strings");
          }
          // TODO: string pool lookup
          fields.push_back("<string:" + std::to_string(value.asStringValId()) + ">");
        }
        uint32_t type_id = registerClassType(name, fields);
        class_type_ids_by_name_[name] = type_id;
        return BytecodeValue::makeInt(static_cast<int64_t>(type_id));
      });

  registerHostFunction(
      "class.new", [this](const std::vector<BytecodeValue> &args) {
        if (args.empty()) {
          throw std::runtime_error(
              "class.new(type, ...values) requires a type argument");
        }
        uint32_t type_id = 0;
        if (args[0].isInt()) {
          type_id = static_cast<uint32_t>(args[0].asInt());
        } else if (args[0].isStringValId()) {
          // TODO: string pool lookup
          const auto &name = "<string:" + std::to_string(args[0].asStringValId()) + ">";
          auto it = class_type_ids_by_name_.find(name);
          if (it == class_type_ids_by_name_.end()) {
            throw std::runtime_error("Unknown class type: " + name);
          }
          type_id = it->second;
        } else {
          throw std::runtime_error("class.new type must be string or int");
        }

        const size_t field_count = heap_.classFieldCount(type_id);
        ClassRef ref = createClass(type_id, field_count);
        const size_t provided = args.size() - 1;
        for (size_t i = 0; i < provided && i < field_count; ++i) {
          setClassField(ref, i, args[i + 1]);
        }
        return BytecodeValue(ref);
      });

  registerHostFunction(
      "class.get", [this](const std::vector<BytecodeValue> &args) {
        if (args.size() != 2 || !args[0].isClassId()) {
          throw std::runtime_error("class.get(class, field) expects class");
        }
        ClassRef ref{args[0].asClassId()};
        size_t index = 0;
        if (args[1].isInt()) {
          index = static_cast<size_t>(args[1].asInt());
        } else if (args[1].isStringValId()) {
          // TODO: string pool lookup
          auto idx =
              heap_.classFieldIndex(ref.typeId, "<string:" + std::to_string(args[1].asStringValId()) + ">");
          if (!idx.has_value()) {
            return BytecodeValue::makeNull();
          }
          index = *idx;
        } else {
          throw std::runtime_error("class.get field must be string or int");
        }
        return getClassField(ref, index);
      });

  registerHostFunction(
      "class.set", [this](const std::vector<BytecodeValue> &args) {
        if (args.size() != 3 || !args[0].isClassId()) {
          throw std::runtime_error(
              "class.set(class, field, value) expects class");
        }
        ClassRef ref{args[0].asClassId()};
        size_t index = 0;
        if (args[1].isInt()) {
          index = static_cast<size_t>(args[1].asInt());
        } else if (args[1].isStringValId()) {
          // TODO: string pool lookup
          auto idx =
              heap_.classFieldIndex(ref.typeId, "<string:" + std::to_string(args[1].asStringValId()) + ">");
          if (!idx.has_value()) {
            throw std::runtime_error("Unknown class field name");
          }
          index = *idx;
        } else {
          throw std::runtime_error("class.set field must be string or int");
        }
        setClassField(ref, index, args[2]);
        return BytecodeValue(ref);
      });
}

void VM::registerDefaultHostGlobals() {
  std::cerr << "[DEBUG] registerDefaultHostGlobals called\n";
  auto system_obj = heap_.allocateObject();
  setHostObjectField(system_obj, "gc", HostFunctionRef{.name = "system.gc"});
  setHostObjectField(system_obj, "gcStats",
                     HostFunctionRef{.name = "system.gcStats"});
  setGlobal("system", system_obj);

  auto struct_obj = heap_.allocateObject();
  setHostObjectField(struct_obj, "define",
                     HostFunctionRef{.name = "struct.define"});
  setHostObjectField(struct_obj, "new", HostFunctionRef{.name = "struct.new"});
  setHostObjectField(struct_obj, "get", HostFunctionRef{.name = "struct.get"});
  setHostObjectField(struct_obj, "set", HostFunctionRef{.name = "struct.set"});
  std::cerr << "[DEBUG] struct object created, setting global\n";
  setGlobal("struct", struct_obj);
  std::cerr << "[DEBUG] struct global set\n";

  auto class_obj = heap_.allocateObject();
  setHostObjectField(class_obj, "define",
                     HostFunctionRef{.name = "class.define"});
  setHostObjectField(class_obj, "new", HostFunctionRef{.name = "class.new"});
  setHostObjectField(class_obj, "get", HostFunctionRef{.name = "class.get"});
  setHostObjectField(class_obj, "set", HostFunctionRef{.name = "class.set"});
  std::cerr << "[DEBUG] class object created, setting global\n";
  setGlobal("class", class_obj);
  std::cerr << "[DEBUG] class global set\n";

  // Register default window globals (will be updated by WindowMonitor)
  // TODO: string pool registration for default strings
  setGlobal("title", Value::makeNull());
  setGlobal("exe", Value::makeNull());
  setGlobal("pid", Value::makeInt(0));

  // Call system object initializer if provided (adds module-specific fields)
  std::cerr << "[DEBUG] system_object_initializer_ check: "
            << (system_object_initializer_ ? "set" : "not set") << "\n";
  if (system_object_initializer_) {
    std::cerr << "[DEBUG] Calling system_object_initializer_\n";
    system_object_initializer_(this);
    std::cerr << "[DEBUG] system_object_initializer_ completed\n";
  }
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

BytecodeValue VM::execute(const BytecodeChunk &chunk,
                          const std::string &function_name,
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
  frame_count_ = 0;
  heap_.reset();
  struct_type_ids_by_name_.clear();
  class_type_ids_by_name_.clear();
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
    std::cout << "=== Executing function: " << function_name
              << " ===" << std::endl;
  }

  runDispatchLoop(0);

  if (stack.empty()) {
    return nullptr;
  }

  BytecodeValue result = stack.top();
  stack.pop();
  return result;
}

BytecodeValue VM::executePersistent(const BytecodeChunk &chunk,
                                    const std::string &function_name,
                                    const std::vector<BytecodeValue> &args) {
  current_chunk = &chunk;

  const auto *entry = chunk.getFunction(function_name);
  if (!entry) {
    throw std::runtime_error("Function not found: " + function_name);
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
      throw std::runtime_error("Argument count mismatch for entry function '" +
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

  BytecodeValue result = stack.top();
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
    uint32_t previous_ip = ip;

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
            const auto &loc = frame.function->instruction_locations[frame.ip];
            line = loc.line;
            column = loc.column;
          }
        }

        std::string errorMsg =
            "Uncaught exception: " + toString(thrown.value, &heap_);
        if (line > 0) {
          errorMsg += " at line " + std::to_string(line);
          if (column > 0) {
            errorMsg += ":" + std::to_string(column);
          }
        }

        throw ScriptError(thrown.value, errorMsg, stackTrace, line, column);
      }
      continue;
    }

    processPendingCalls();

    // CRITICAL: Re-fetch frame AFTER executeInstruction (vector may have
    // reallocated)
    if (frame_count_ > stop_frame_depth) {
      active_frame_idx = frame_count_ - 1;
      if (frame_arena_[active_frame_idx].ip == previous_ip) {
        frame_arena_[active_frame_idx].ip++;
      }
    }
  }
}

bool VM::handleScriptThrow(const BytecodeValue &value) {
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

BytecodeValue VM::call(const BytecodeValue &callee_value,
                       const std::vector<BytecodeValue> &args) {
  if (!current_chunk) {
    throw std::runtime_error(
        "VM::call requires an active bytecode chunk (run execute first)");
  }

  const size_t base_depth = frame_count_;
  doCall(callee_value, args, false);
  runDispatchLoop(base_depth);

  if (stack.empty()) {
    return nullptr;
  }
  BytecodeValue result = stack.top();
  stack.pop();
  return result;
}

void VM::setDebugMode(bool enabled) { debug_mode = enabled; }

void VM::doCall(BytecodeValue callee_value, std::vector<BytecodeValue> args,
                bool advance_caller_ip) {
  if (callee_value.isHostFuncId()) {
    // TODO: host func name lookup
    (void)callee_value.asHostFuncId();
    throw std::runtime_error("Host function call via doCall not yet supported with NaN boxing");
  }

  if (frame_count_ >= max_call_depth_) {
    throw std::runtime_error("Stack overflow: maximum call depth " +
                             std::to_string(max_call_depth_) + " reached");
  }

  // Handle host function call (duplicate check after depth check)
  if (callee_value.isHostFuncId()) {
    // TODO: host func name lookup
    (void)callee_value.asHostFuncId();
    throw std::runtime_error("Host function call via doCall not yet supported with NaN boxing");
  }

  uint32_t function_index = 0;
  uint32_t closure_id = 0;
  if (callee_value.isFunctionObjId()) {
    function_index = callee_value.asFunctionObjId();
  } else if (callee_value.isClosureId()) {
    closure_id = callee_value.asClosureId();
    auto *closure = heap_.closure(closure_id);
    if (!closure) {
      throw std::runtime_error("Closure not found: " +
                               std::to_string(closure_id));
    }
    function_index = closure->function_index;
  } else {
    throw std::runtime_error("CALL expects function or closure as callee");
  }

  const auto *callee = current_chunk->getFunction(function_index);
  if (!callee) {
    throw std::runtime_error("Function index not found: " +
                             std::to_string(function_index));
  }

  // Debug
  if (callee_value.isFunctionObjId()) {
    // Check if function is valid
  }

  // Allow fewer arguments than parameters (for default parameters)
  // For variadic functions, allow MORE arguments than parameters
  if (callee->variadic_param_index == UINT32_MAX &&
      args.size() > callee->param_count) {
    throw std::runtime_error("Argument count mismatch calling function index " +
                             std::to_string(function_index) +
                             " (expected at most " +
                             std::to_string(callee->param_count) + ", got " +
                             std::to_string(args.size()) + ")");
  }

  // For variadic functions, require at least as many args as non-variadic
  // params
  if (callee->variadic_param_index != UINT32_MAX &&
      args.size() < callee->variadic_param_index) {
    throw std::runtime_error("Argument count mismatch calling function index " +
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
      locals[base + i] = BytecodeValue(arrRef);
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

void VM::doTailCall(BytecodeValue callee_value,
                    std::vector<BytecodeValue> args) {
  // Tail call optimization: reuse current frame instead of pushing new one
  if (callee_value.isHostFuncId()) {
    // TODO: host func name lookup
    (void)callee_value.asHostFuncId();
    throw std::runtime_error("Host function tail call not yet supported with NaN boxing");
  }

  uint32_t function_index = 0;
  uint32_t closure_id = 0;
  if (callee_value.isFunctionObjId()) {
    function_index = callee_value.asFunctionObjId();
  } else if (callee_value.isClosureId()) {
    closure_id = callee_value.asClosureId();
    auto *closure = heap_.closure(closure_id);
    if (!closure) {
      throw std::runtime_error("Closure not found: " +
                               std::to_string(closure_id));
    }
    function_index = closure->function_index;
  } else {
    throw std::runtime_error("TAIL_CALL expects function or closure as callee");
  }

  const auto *callee = current_chunk->getFunction(function_index);
  if (!callee) {
    throw std::runtime_error("Function index not found: " +
                             std::to_string(function_index));
  }

  // Allow fewer arguments than parameters (for default parameters)
  // For variadic functions, allow MORE arguments than parameters
  if (callee->variadic_param_index == UINT32_MAX &&
      args.size() > callee->param_count) {
    throw std::runtime_error(
        "Argument count mismatch for tail call to function index " +
        std::to_string(function_index) + " (expected at most " +
        std::to_string(callee->param_count) + ", got " +
        std::to_string(args.size()) + ")");
  }

  // For variadic functions, require at least as many args as non-variadic
  // params
  if (callee->variadic_param_index != UINT32_MAX &&
      args.size() < callee->variadic_param_index) {
    throw std::runtime_error(
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
      locals[old_base + i] = BytecodeValue(arrRef);
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

std::vector<BytecodeValue> VM::stackValuesForRoots() const {
  std::vector<BytecodeValue> values;
  std::stack<BytecodeValue> copy = stack;
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
      [this](uint32_t index) -> std::optional<BytecodeValue> {
        if (index >= locals.size()) {
          return std::nullopt;
        }
        return locals[index];
      });
}

void VM::collectGarbage() {
  heap_.collectGarbage(stackValuesForRoots(), locals, globals,
                       activeClosureIdsForRoots(),
                       [this](uint32_t index) -> std::optional<BytecodeValue> {
                         if (index >= locals.size()) {
                           return std::nullopt;
                         }
                         return locals[index];
                       });
}

void VM::executeInstruction(const Instruction &instruction) {
  auto pop = [this]() -> BytecodeValue {
    if (stack.empty()) {
      throw std::runtime_error("Stack underflow");
    }

    BytecodeValue value = stack.top();
    stack.pop();
    return value;
  };

  auto push = [this](BytecodeValue value) { stack.push(std::move(value)); };

  auto toAbsoluteLocal = [this](uint32_t local_index) -> uint32_t {
    return static_cast<uint32_t>(currentFrame().locals_base + local_index);
  };

  auto ensureLocalIndex = [this](uint32_t absolute_index) {
    if (absolute_index >= locals.size()) {
      locals.resize(static_cast<size_t>(absolute_index) + 1, nullptr);
    }
  };

  auto doReturn = [this, &pop, &push]() {
    if (frame_count_ == 0) {
      return;
    }

    BytecodeValue ret = nullptr;
    if (!stack.empty()) {
      ret = pop();
    }

    auto finished = frame_arena_[frame_count_ - 1];
    frame_count_--;

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

  case OpCode::LOAD_GLOBAL: {
    if (instruction.operands.empty() ||
        !instruction.operands[0].isStringValId()) {
      throw std::runtime_error("LOAD_GLOBAL expects string operand");
    }
    const auto &name = instruction.operands[0].toString();
    auto it = globals.find(name);
    push(it == globals.end() ? BytecodeValue::makeNull() : it->second);
    break;
  }

  case OpCode::STORE_GLOBAL: {
    if (instruction.operands.empty() ||
        !instruction.operands[0].isStringValId()) {
      throw std::runtime_error("STORE_GLOBAL expects string operand");
    }
    const auto &name = instruction.operands[0].toString();
    BytecodeValue value = pop();

    // Value type semantics: copy structs on assignment
    if (value.isStructId()) {
      value = BytecodeValue(copyStruct(StructRef{value.asStructId()}));
    }

    globals[name] = value;
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
    BytecodeValue value = pop();

    // Value type semantics: copy structs on assignment
    if (value.isStructId()) {
      value = BytecodeValue(copyStruct(StructRef{value.asStructId()}));
    }

    locals[abs] = value;
    break;
  }

  case OpCode::LOAD_UPVALUE: {
    uint32_t upvalue_index = std::get<uint32_t>(instruction.operands[0]);
    uint32_t closure_id = currentFrame().closure_id;
    if (closure_id == 0) {
      throw std::runtime_error("LOAD_UPVALUE used without active closure");
    }
    auto *closure = heap_.closure(closure_id);
    if (!closure) {
      throw std::runtime_error("Closure not found for LOAD_UPVALUE");
    }
    if (upvalue_index >= closure->upvalues.size() ||
        !closure->upvalues[upvalue_index]) {
      throw std::runtime_error("LOAD_UPVALUE index out of range");
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
    uint32_t upvalue_index = std::get<uint32_t>(instruction.operands[0]);
    uint32_t closure_id = currentFrame().closure_id;
    if (closure_id == 0) {
      throw std::runtime_error("STORE_UPVALUE used without active closure");
    }
    auto *closure = heap_.closure(closure_id);
    if (!closure) {
      throw std::runtime_error("Closure not found for STORE_UPVALUE");
    }
    if (upvalue_index >= closure->upvalues.size() ||
        !closure->upvalues[upvalue_index]) {
      throw std::runtime_error("STORE_UPVALUE index out of range");
    }
    auto &cell = closure->upvalues[upvalue_index];
    BytecodeValue value = pop();

    // Value type semantics: copy structs on assignment
    if (value.isStructId()) {
      value = BytecodeValue(copyStruct(StructRef{value.asStructId()}));
    }

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

  case OpCode::PUSH_NULL: {
    push(Value::makeNull());
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
        // Null cannot be ordered with anything
        result = false;
        break;
      default:
        throw std::runtime_error("Invalid comparison opcode with null");
      }
      push(result);
      break;
    }

    if (left.isInt() && right.isInt()) {
      int64_t l = left.asInt();
      int64_t r = right.asInt();
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
          throw ScriptThrow{BytecodeValue("Division by zero")};
        }
        push(l / r);
        break;
      case OpCode::MOD:
        if (r == 0) {
          throw ScriptThrow{BytecodeValue("Modulo by zero")};
        }
        push(l % r);
        break;
      case OpCode::POW:
        // Integer power: use std::pow and convert back
        push(static_cast<int64_t>(
            std::pow(static_cast<double>(l), static_cast<double>(r))));
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

    if ((left.isInt() || left.isDouble()) &&
        (right.isInt() || right.isDouble())) {
      double l = left.isInt()
                     ? static_cast<double>(left.asInt())
                     : left.asDouble();
      double r = right.isInt()
                     ? static_cast<double>(right.asInt())
                     : right.asDouble();

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
          throw ScriptThrow{BytecodeValue("Division by zero")};
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

    if (left.isStringValId() && right.isStringValId()) {
      // TODO: string pool lookup - for now use placeholder
      const std::string l = "<string:" + std::to_string(left.asStringValId()) + ">";
      const std::string r = "<string:" + std::to_string(right.asStringValId()) + ">";

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

    // String coercion for ADD: string + any or any + string
    if (instruction.opcode == OpCode::ADD) {
      if (left.isStringValId()) {
        // string + any - convert right to string
        // TODO: string pool lookup
        std::string l = "<string:" + std::to_string(left.asStringValId()) + ">";
        push(l + toString(right, &heap_));
        break;
      }
      if (right.isStringValId()) {
        // any + string - convert left to string
        // TODO: string pool lookup
        std::string r = "<string:" + std::to_string(right.asStringValId()) + ">";
        push(toString(left, &heap_) + r);
        break;
      }
    }

    throw std::runtime_error("Type mismatch in binary operation");
  }

  case OpCode::AND: {
    BytecodeValue right = pop();
    BytecodeValue left = pop();
    push(isTruthy(left) && isTruthy(right));
    break;
  }

  case OpCode::OR: {
    BytecodeValue right = pop();
    BytecodeValue left = pop();
    push(isTruthy(left) || isTruthy(right));
    break;
  }

  case OpCode::NOT: {
    BytecodeValue value = pop();
    push(!isTruthy(value));
    break;
  }

  case OpCode::NEGATE: {
    BytecodeValue value = pop();
    if (value.isInt()) {
      push(-value.asInt());
    } else if (value.isDouble()) {
      push(-value.asDouble());
    } else {
      throw std::runtime_error("Cannot negate non-numeric value");
    }
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
    if (!isTruthy(condition)) {
      currentFrame().ip = target;
    }
    break;
  }

  case OpCode::JUMP_IF_TRUE: {
    uint32_t target = std::get<uint32_t>(instruction.operands[0]);
    BytecodeValue condition = pop();
    if (isTruthy(condition)) {
      currentFrame().ip = target;
    }
    break;
  }

  case OpCode::IS_NULL: {
    BytecodeValue value = pop();
    bool isNullVal = value.isNull();
    push(Value::makeBool(isNullVal));
    break;
  }

  case OpCode::JUMP_IF_NULL: {
    uint32_t target = std::get<uint32_t>(instruction.operands[0]);
    BytecodeValue value = pop();
    // Only jump on null/undefined, not on all falsy values
    if (value.isNull()) {
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

  case OpCode::TAIL_CALL: {
    // Tail call optimization: reuse current frame instead of pushing new one
    uint32_t arg_count = std::get<uint32_t>(instruction.operands[0]);
    if (stack.size() < static_cast<size_t>(arg_count) + 1) {
      throw std::runtime_error("Stack underflow during TAIL_CALL");
    }

    std::vector<BytecodeValue> args(arg_count);
    for (uint32_t i = 0; i < arg_count; ++i) {
      args[arg_count - 1 - i] = pop();
    }
    BytecodeValue callee_value = pop();

    doTailCall(callee_value, std::move(args));
    break;
  }

  case OpCode::CALL_HOST: {
    if (instruction.operands.size() != 2 ||
        !instruction.operands[0].isStringValId() ||
        !std::holds_alternative<uint32_t>(instruction.operands[1])) {
      throw std::runtime_error("CALL_HOST expects operands: <string "
                               "function_name, uint32 arg_count>");
    }

    const std::string &function_name =
        instruction.operands[0].toString();
    uint32_t arg_count = std::get<uint32_t>(instruction.operands[1]);
    push(invokeHostFunction(function_name, arg_count));
    break;
  }

  case OpCode::CALL_SUPER: {
    // CALL_SUPER: operands are [method_name, arg_count]
    // Pops args from stack, looks up parent class method, calls it
    if (instruction.operands.size() != 2 ||
        !instruction.operands[0].isStringValId() ||
        !std::holds_alternative<uint32_t>(instruction.operands[1])) {
      throw std::runtime_error("CALL_SUPER expects operands: <string "
                               "method_name, uint32 arg_count>");
    }

    const std::string &method_name =
        instruction.operands[0].toString();
    uint32_t arg_count = std::get<uint32_t>(instruction.operands[1]);

    if (stack.size() < static_cast<size_t>(arg_count)) {
      throw std::runtime_error("Stack underflow during CALL_SUPER");
    }

    // Pop arguments from stack
    std::vector<BytecodeValue> args(arg_count);
    for (uint32_t i = 0; i < arg_count; ++i) {
      args[arg_count - 1 - i] = pop();
    }

    // Get current 'this' from local scope (slot 0 typically)
    size_t base = currentFrame().locals_base;
    BytecodeValue this_value = locals[base + 0];

    // Find the parent class method using the prototype chain
    // For now, emit as a host function call with special prefix
    // Full implementation needs parent method lookup via heap_.findClassMethod
    std::string super_method_name = "super." + method_name;

    // Prepend 'this' to args
    args.insert(args.begin(), this_value);

    // Call as host function - runtime will need to resolve via parent class
    push(invokeHostFunction(super_method_name,
                            static_cast<uint32_t>(args.size())));
    break;
  }

  case OpCode::RETURN: {
    doReturn();
    break;
  }

  case OpCode::TRY_ENTER: {
    if (instruction.operands.size() < 1 ||
        !std::holds_alternative<uint32_t>(instruction.operands[0])) {
      throw std::runtime_error("TRY_ENTER expects catch ip operand");
    }
    const uint32_t catch_ip = std::get<uint32_t>(instruction.operands[0]);
    uint32_t finally_ip = 0;
    if (instruction.operands.size() >= 2 &&
        std::holds_alternative<uint32_t>(instruction.operands[1])) {
      finally_ip = std::get<uint32_t>(instruction.operands[1]);
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
      push(current_exception_);
    } else {
      push(nullptr);
    }
    break;
  }

  case OpCode::THROW: {
    BytecodeValue thrown = pop();
    throw ScriptThrow{std::move(thrown)};
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
          throw std::runtime_error(
              "CLOSURE tried to capture upvalue without parent closure");
        }
        auto *parent_closure = heap_.closure(parent_closure_id);
        if (!parent_closure) {
          throw std::runtime_error("Parent closure not found for CLOSURE");
        }
        if (descriptor.index >= parent_closure->upvalues.size()) {
          throw std::runtime_error("CLOSURE upvalue index out of range");
        }
        closure.upvalues.push_back(parent_closure->upvalues[descriptor.index]);
      }
    }

    push(heap_.allocateClosure(
        GCHeap::RuntimeClosure{.function_index = closure.function_index,
                               .upvalues = std::move(closure.upvalues)}));
    maybeCollectGarbage();
    break;
  }

  case OpCode::ARRAY_NEW: {
    push(heap_.allocateArray());
    maybeCollectGarbage();
    break;
  }

  case OpCode::SET_NEW: {
    push(heap_.allocateSet());
    maybeCollectGarbage();
    break;
  }

  case OpCode::ARRAY_PUSH: {
    BytecodeValue value = pop();
    BytecodeValue container = pop();
    if (!container.isArrayId()) {
      throw std::runtime_error("ARRAY_PUSH expects array container");
    }
    uint32_t id = container.asArrayId();
    auto *array = heap_.array(id);
    if (!array) {
      throw std::runtime_error("ARRAY_PUSH unknown array id");
    }
    array->push_back(value);
    push(container);
    break;
  }

  case OpCode::ARRAY_LEN: {
    BytecodeValue container = pop();
    if (!container.isArrayId()) {
      throw std::runtime_error("ARRAY_LEN expects array container");
    }
    uint32_t id = container.asArrayId();
    auto *array = heap_.array(id);
    if (!array) {
      throw std::runtime_error("ARRAY_LEN unknown array id");
    }
    push(Value::makeInt(static_cast<int64_t>(array->size())));
    break;
  }

  // Range creation: start..end or start..step..end
  case OpCode::RANGE_NEW: {
    int64_t end = pop().asInt();
    int64_t start = pop().asInt();
    RangeRef rangeRef = heap_.allocateRange(start, end, 1);
    push(Value::makeRangeId(rangeRef.id));
    break;
  }

  case OpCode::RANGE_STEP_NEW: {
    int64_t step = pop().asInt();
    int64_t end = pop().asInt();
    int64_t start = pop().asInt();
    RangeRef rangeRef = heap_.allocateRange(start, end, step);
    push(Value::makeRangeId(rangeRef.id));
    break;
  }

  // Struct operations
  case OpCode::STRUCT_NEW: {
    // Operands: typeId (uint32), fieldCount (uint32)
    uint32_t typeId = std::get<uint32_t>(instruction.operands[0]);
    uint32_t fieldCount = std::get<uint32_t>(instruction.operands[1]);
    StructRef structRef = heap_.allocateStruct(typeId, fieldCount);
    push(Value::makeStructId(structRef.id));
    break;
  }

  case OpCode::STRUCT_GET: {
    // Pop: struct ref, index
    BytecodeValue indexVal = pop();
    BytecodeValue structVal = pop();
    if (!structVal.isStructId() || !indexVal.isInt()) {
      throw std::runtime_error("STRUCT_GET expects struct and int index");
    }
    auto structRef = StructRef{structVal.asStructId()};
    size_t index = static_cast<size_t>(indexVal.asInt());
    push(heap_.structs_.at(structRef.id).at(index));
    break;
  }

  case OpCode::STRUCT_SET: {
    // Pop: struct ref, index, value
    BytecodeValue value = pop();
    BytecodeValue indexVal = pop();
    BytecodeValue structVal = pop();
    if (!structVal.isStructId() || !indexVal.isInt()) {
      throw std::runtime_error(
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
    uint32_t typeId = std::get<uint32_t>(instruction.operands[0]);
    uint32_t tag = std::get<uint32_t>(instruction.operands[1]);
    uint32_t payloadCount = std::get<uint32_t>(instruction.operands[2]);
    EnumRef enumRef = heap_.allocateEnum(typeId, tag, payloadCount);
    push(Value::makeEnumId(enumRef.id));
    break;
  }

  case OpCode::ENUM_TAG: {
    BytecodeValue enumVal = pop();
    if (!enumVal.isEnumId()) {
      throw std::runtime_error("ENUM_TAG expects enum");
    }
    auto enumRef = EnumRef{enumVal.asEnumId(), 0, 0};
    push(Value::makeInt(static_cast<int64_t>(enumRef.tag)));
    break;
  }

  case OpCode::ENUM_PAYLOAD: {
    BytecodeValue indexVal = pop();
    BytecodeValue enumVal = pop();
    if (!enumVal.isEnumId() || !indexVal.isInt()) {
      throw std::runtime_error("ENUM_PAYLOAD expects enum and int index");
    }
    auto enumRef = EnumRef{enumVal.asEnumId(), 0, 0};
    size_t index = static_cast<size_t>(indexVal.asInt());
    push(heap_.enums_.at(enumRef.id).second.at(index));
    break;
  }

  case OpCode::ENUM_MATCH: {
    // Pop: enum ref, expected tag
    BytecodeValue tagVal = pop();
    BytecodeValue enumVal = pop();
    if (!enumVal.isEnumId() || !tagVal.isInt()) {
      throw std::runtime_error("ENUM_MATCH expects enum and int tag");
    }
    auto enumRef = EnumRef{enumVal.asEnumId(), 0, 0};
    int64_t expectedTag = tagVal.asInt();
    push(Value::makeBool(enumRef.tag == static_cast<uint32_t>(expectedTag)));
    break;
  }

  // Iteration protocol: iter(obj) → iterator
  case OpCode::ITER_NEW: {
    BytecodeValue iterable = pop();

    // Create iterator based on type
    IteratorRef iterRef;
    iterRef.id = heap_.createIterator(iterable);
    push(Value::makeIteratorId(iterRef.id));
    break;
  }

  // Iteration protocol: iterator.next() → {value, done}
  case OpCode::ITER_NEXT: {
    BytecodeValue iterator_val = pop();
    if (!iterator_val.isIteratorId()) {
      throw std::runtime_error("ITER_NEXT expects iterator");
    }

    uint32_t id = iterator_val.asIteratorId();
    auto result = heap_.iteratorNext(id);

    // result is {value, done} object
    push(result);
    break;
  }

  case OpCode::ARRAY_GET: {
    BytecodeValue index_or_key = pop();
    BytecodeValue container = pop();

    if (container.isArrayId()) {
      auto index = indexFromValue(index_or_key);
      if (!index) {
        throw std::runtime_error("ARRAY_GET expects integer index");
      }
      auto *array = heap_.array(container.asArrayId());
      if (!array) {
        throw std::runtime_error("ARRAY_GET unknown array id");
      }
      // Handle negative indices: -1 = last element, -2 = second to last, etc.
      int64_t idx = *index;
      if (idx < 0) {
        idx = static_cast<int64_t>(array->size()) + idx;
      }
      if (idx < 0 || static_cast<size_t>(idx) >= array->size()) {
        push(Value::makeNull());
      } else {
        push((*array)[static_cast<size_t>(idx)]);
      }
      break;
    }

    if (container.isSetId()) {
      auto key = keyFromValue(index_or_key);
      if (!key) {
        throw std::runtime_error(
            "SET membership expects string/number/bool key");
      }
      auto *set = heap_.set(container.asSetId());
      if (!set) {
        throw std::runtime_error("ARRAY_GET unknown set id");
      }
      push(set->find(*key) != set->end());
      break;
    }

    if (container.isObjectId()) {
      auto key = keyFromValue(index_or_key);
      if (!key) {
        throw std::runtime_error("OBJECT index expects string/number/bool key");
      }
      auto *object = heap_.object(container.asObjectId());
      if (!object) {
        throw std::runtime_error("ARRAY_GET unknown object id");
      }
      auto kv = object->find(*key);
      push(kv == object->end() ? Value::makeNull() : kv->second);
      break;
    }

    throw std::runtime_error("ARRAY_GET expects array/set/object container");
  }

  case OpCode::ARRAY_SET: {
    BytecodeValue value = pop();
    BytecodeValue index_or_key = pop();
    BytecodeValue container = pop();

    if (container.isArrayId()) {
      auto index = indexFromValue(index_or_key);
      if (!index) {
        throw std::runtime_error("ARRAY_SET expects integer index");
      }
      auto *array = heap_.array(container.asArrayId());
      if (!array) {
        throw std::runtime_error("ARRAY_SET unknown array id");
      }
      // Handle negative indices: -1 = last element, etc.
      int64_t idx = *index;
      if (idx < 0) {
        idx = static_cast<int64_t>(array->size()) + idx;
      }
      if (idx < 0) {
        throw std::runtime_error("ARRAY_SET index out of bounds");
      }
      const auto idx_size = static_cast<size_t>(idx);
      if (idx_size >= array->size()) {
        array->resize(idx_size + 1, Value::makeNull());
      }
      (*array)[idx_size] = value;
      break;
    }

    if (container.isSetId()) {
      auto key = keyFromValue(index_or_key);
      if (!key) {
        throw std::runtime_error(
            "SET assignment expects string/number/bool key");
      }
      auto *set = heap_.set(container.asSetId());
      if (!set) {
        throw std::runtime_error("ARRAY_SET unknown set id");
      }
      bool present = false;
      if (value.isBool()) {
        present = value.asBool();
      } else if (value.isInt()) {
        present = value.asInt() != 0;
      } else if (value.isDouble()) {
        present = value.asDouble() != 0.0;
      } else {
        throw std::runtime_error(
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
      auto key = keyFromValue(index_or_key);
      if (!key) {
        throw std::runtime_error("OBJECT index assignment expects valid key");
      }
      auto *object = heap_.object(container.asObjectId());
      if (!object) {
        throw std::runtime_error("ARRAY_SET unknown object id");
      }
      (*object)[*key] = value;
      break;
    }

    throw std::runtime_error("ARRAY_SET expects array/set/object container");
  }

  case OpCode::OBJECT_NEW: {
    push(heap_.allocateObject(true)); // sorted = true
    maybeCollectGarbage();
    break;
  }

  case OpCode::OBJECT_NEW_UNSORTED: {
    push(heap_.allocateObject(false)); // sorted = false
    maybeCollectGarbage();
    break;
  }

  case OpCode::OBJECT_GET: {
    BytecodeValue key_value = pop();
    BytecodeValue object = pop();
    if (!object.isObjectId()) {
      throw std::runtime_error("OBJECT_GET expects object container");
    }
    auto objRef = ObjectRef{object.asObjectId(), true};
    auto *obj = heap_.object(objRef.id);
    if (!obj) {
      throw std::runtime_error("OBJECT_GET unknown object id");
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
          push(*val);
        } else {
          push(Value::makeNull());
        }
      } else {
        push(Value::makeNull());
      }
      break;
    }

    auto key = keyFromValue(key_value);
    if (!key) {
      throw std::runtime_error("OBJECT_GET expects string/number/bool key");
    }
    auto *val = obj->get(*key);
    if (val) {
      push(*val);
    } else {
      // Property not found - check prototype methods
      auto method = getPrototypeMethod(object, *key);
      if (method) {
        push(Value::makeHostFuncId(0)); // TODO: proper host func id
      } else {
        push(Value::makeNull());
      }
    }
    break;
  }

  case OpCode::OBJECT_SET: {
    // Stack: [..., obj, value, key] → pops all, pushes obj
    // This allows chaining: obj { DUP, val1, "k1", SET, val2, "k2", SET, ... }
    BytecodeValue key = pop();
    BytecodeValue value = pop();
    BytecodeValue object = pop();

    if (!object.isObjectId()) {
      throw std::runtime_error("OBJECT_SET expects object container");
    }
    auto keyStr = keyFromValue(key);
    if (!keyStr) {
      throw std::runtime_error("OBJECT_SET expects string/number/bool key");
    }

    // Safety: reject __ prefixed keys (reserved for internal use)
    if (keyStr->size() >= 2 && (*keyStr)[0] == '_' && (*keyStr)[1] == '_') {
      throw std::runtime_error(
          "OBJECT_SET: keys starting with '__' are reserved");
    }

    auto *obj = heap_.object(object.asObjectId());
    if (!obj) {
      throw std::runtime_error("OBJECT_SET unknown object id");
    }
    obj->set(*keyStr, std::move(value));
    push(object); // Return the object for chaining
    break;
  }

  // Object intrinsics (VM-level operations)
  case OpCode::OBJECT_KEYS: {
    BytecodeValue object = pop();
    if (!object.isObjectId()) {
      throw std::runtime_error("OBJECT_KEYS expects object");
    }
    auto *obj = heap_.object(object.asObjectId());
    if (!obj) {
      throw std::runtime_error("OBJECT_KEYS unknown object id");
    }
    auto arrRef = heap_.allocateArray();
    auto *arr = heap_.array(arrRef.id);
    auto keys = obj->getKeys();
    for (const auto &key : keys) {
      // TODO: string pool registration
      arr->push_back(Value::makeNull());
    }
    push(Value::makeArrayId(arrRef.id));
    break;
  }

  case OpCode::OBJECT_VALUES: {
    BytecodeValue object = pop();
    if (!object.isObjectId()) {
      throw std::runtime_error("OBJECT_VALUES expects object");
    }
    auto *obj = heap_.object(object.asObjectId());
    if (!obj) {
      throw std::runtime_error("OBJECT_VALUES unknown object id");
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
    push(Value::makeArrayId(arrRef.id));
    break;
  }

  case OpCode::OBJECT_ENTRIES: {
    BytecodeValue object = pop();
    if (!object.isObjectId()) {
      throw std::runtime_error("OBJECT_ENTRIES expects object");
    }
    auto *obj = heap_.object(object.asObjectId());
    if (!obj) {
      throw std::runtime_error("OBJECT_ENTRIES unknown object id");
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
    push(Value::makeArrayId(arrRef.id));
    break;
  }

  case OpCode::OBJECT_HAS: {
    BytecodeValue keyValue = pop();
    BytecodeValue object = pop();
    if (!object.isObjectId()) {
      throw std::runtime_error("OBJECT_HAS expects object");
    }
    auto key = keyFromValue(keyValue);
    if (!key) {
      throw std::runtime_error("OBJECT_HAS expects string/number/bool key");
    }
    auto *obj = heap_.object(object.asObjectId());
    if (!obj) {
      push(Value::makeBool(false));
    } else {
      push(Value::makeBool(obj->get(*key) != nullptr));
    }
    break;
  }

  case OpCode::OBJECT_DELETE: {
    BytecodeValue keyValue = pop();
    BytecodeValue object = pop();
    if (!object.isObjectId()) {
      throw std::runtime_error("OBJECT_DELETE expects object");
    }
    auto key = keyFromValue(keyValue);
    if (!key) {
      throw std::runtime_error("OBJECT_DELETE expects string/number/bool key");
    }
    auto *obj = heap_.object(object.asObjectId());
    if (!obj) {
      push(Value::makeBool(false));
    } else {
      push(Value::makeBool(obj->data.erase(*key) > 0));
    }
    break;
  }

  // Array intrinsics (VM-level operations)
  case OpCode::ARRAY_POP: {
    BytecodeValue array = pop();
    if (!array.isArrayId()) {
      throw std::runtime_error("ARRAY_POP expects array");
    }
    auto *arr = heap_.array(array.asArrayId());
    if (!arr || arr->empty()) {
      push(Value::makeNull());
    } else {
      push(arr->back());
      arr->pop_back();
    }
    break;
  }

  case OpCode::ARRAY_HAS: {
    BytecodeValue value = pop();
    BytecodeValue array = pop();
    if (!array.isArrayId()) {
      throw std::runtime_error("ARRAY_HAS expects array");
    }
    auto *arr = heap_.array(array.asArrayId());
    if (!arr) {
      push(Value::makeBool(false));
    } else {
      bool found = false;
      for (const auto &elem : *arr) {
        if (valuesEqual(elem, value)) {
          found = true;
          break;
        }
      }
      push(Value::makeBool(found));
    }
    break;
  }

  case OpCode::ARRAY_FIND: {
    BytecodeValue value = pop();
    BytecodeValue array = pop();
    if (!array.isArrayId()) {
      throw std::runtime_error("ARRAY_FIND expects array");
    }
    auto *arr = heap_.array(array.asArrayId());
    if (!arr) {
      push(Value::makeInt(-1));
    } else {
      int64_t foundIdx = -1;
      for (size_t i = 0; i < arr->size(); i++) {
        if (valuesEqual((*arr)[i], value)) {
          foundIdx = static_cast<int64_t>(i);
          break;
        }
      }
      push(Value::makeInt(foundIdx));
    }
    break;
  }

  // Array higher-order functions (VM intrinsics)
  case OpCode::ARRAY_MAP: {
    BytecodeValue fn = pop();
    BytecodeValue array = pop();
    if (!array.isArrayId()) {
      throw std::runtime_error("ARRAY_MAP expects array");
    }
    auto *arr = heap_.array(array.asArrayId());
    if (!arr) {
      push(Value::makeNull());
      break;
    }
    if (!fn.isFunctionObjId() && !fn.isClosureId()) {
      throw std::runtime_error("ARRAY_MAP expects function/closure");
    }

    auto resultRef = heap_.allocateArray();
    auto *result = heap_.array(resultRef.id);
    uint64_t resultRootId = pinExternalRoot(Value::makeArrayId(resultRef.id));

    for (size_t i = 0; i < arr->size(); i++) {
      BytecodeValue mapped = callFunctionSync(fn, {(*arr)[i]});
      result->push_back(mapped);
    }

    unpinExternalRoot(resultRootId);
    push(Value::makeArrayId(resultRef.id));
    break;
  }

  case OpCode::ARRAY_FILTER: {
    BytecodeValue fn = pop();
    BytecodeValue array = pop();
    if (!array.isArrayId()) {
      throw std::runtime_error("ARRAY_FILTER expects array");
    }
    auto *arr = heap_.array(array.asArrayId());
    if (!arr) {
      push(Value::makeNull());
      break;
    }

    auto resultRef = heap_.allocateArray();
    auto *result = heap_.array(resultRef.id);
    uint64_t resultRootId = pinExternalRoot(Value::makeArrayId(resultRef.id));

    for (size_t i = 0; i < arr->size(); i++) {
      BytecodeValue predResult = callFunctionSync(fn, {(*arr)[i]});
      if (predResult.isBool() && predResult.asBool()) {
        result->push_back((*arr)[i]);
      }
    }

    unpinExternalRoot(resultRootId);
    push(Value::makeArrayId(resultRef.id));
    break;
  }

  case OpCode::ARRAY_REDUCE: {
    BytecodeValue initial = pop();
    BytecodeValue fn = pop();
    BytecodeValue array = pop();
    if (!array.isArrayId()) {
      throw std::runtime_error("ARRAY_REDUCE expects array");
    }
    auto *arr = heap_.array(array.asArrayId());
    if (!arr) {
      push(initial);
      break;
    }

    BytecodeValue acc = initial;
    for (size_t i = 0; i < arr->size(); i++) {
      acc = callFunctionSync(fn, {acc, (*arr)[i]});
    }

    push(acc);
    break;
  }

  case OpCode::ARRAY_FOREACH: {
    BytecodeValue fn = pop();
    BytecodeValue array = pop();
    if (!array.isArrayId()) {
      throw std::runtime_error("ARRAY_FOREACH expects array");
    }
    auto *arr = heap_.array(array.asArrayId());
    if (!arr) {
      push(Value::makeNull());
      break;
    }

    for (size_t i = 0; i < arr->size(); i++) {
      (void)callFunctionSync(fn, {(*arr)[i]});
    }

    push(Value::makeNull());
    break;
  }

  // String intrinsics (VM-level operations)
  case OpCode::STRING_LEN: {
    BytecodeValue str = pop();
    if (!str.isStringValId()) {
      throw std::runtime_error("STRING_LEN expects string");
    }
    // TODO: string pool lookup - return placeholder length
    std::string s = "<string:" + std::to_string(str.asStringValId()) + ">";
    push(Value::makeInt(static_cast<int64_t>(s.length())));
    break;
  }

  case OpCode::STRING_UPPER: {
    BytecodeValue str = pop();
    if (!str.isStringValId()) {
      throw std::runtime_error("STRING_UPPER expects string");
    }
    // TODO: string pool lookup
    std::string s = "<string:" + std::to_string(str.asStringValId()) + ">";
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    // TODO: string pool registration
    push(Value::makeNull());
    break;
  }

  case OpCode::STRING_LOWER: {
    BytecodeValue str = pop();
    if (!str.isStringValId()) {
      throw std::runtime_error("STRING_LOWER expects string");
    }
    // TODO: string pool lookup
    std::string s = "<string:" + std::to_string(str.asStringValId()) + ">";
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    // TODO: string pool registration
    push(Value::makeNull());
    break;
  }

  case OpCode::STRING_TRIM: {
    BytecodeValue str = pop();
    if (!str.isStringValId()) {
      throw std::runtime_error("STRING_TRIM expects string");
    }
    // TODO: string pool lookup
    std::string s = "<string:" + std::to_string(str.asStringValId()) + ">";
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) {
      push(Value::makeNull());
    } else {
      size_t end = s.find_last_not_of(" \t\n\r");
      // TODO: string pool registration
      push(Value::makeNull());
    }
    break;
  }

  case OpCode::STRING_HAS: {
    BytecodeValue substr = pop();
    BytecodeValue str = pop();
    if (!str.isStringValId() || !substr.isStringValId()) {
      throw std::runtime_error("STRING_HAS expects strings");
    }
    // TODO: string pool lookup
    const std::string s = "<string:" + std::to_string(str.asStringValId()) + ">";
    const std::string sub = "<string:" + std::to_string(substr.asStringValId()) + ">";
    push(Value::makeBool(s.find(sub) != std::string::npos));
    break;
  }

  case OpCode::STRING_STARTS: {
    BytecodeValue prefix = pop();
    BytecodeValue str = pop();
    if (!str.isStringValId() || !prefix.isStringValId()) {
      throw std::runtime_error("STRING_STARTS expects strings");
    }
    // TODO: string pool lookup
    const std::string s = "<string:" + std::to_string(str.asStringValId()) + ">";
    const std::string pre = "<string:" + std::to_string(prefix.asStringValId()) + ">";
    push(Value::makeBool(s.size() >= pre.size() &&
                       s.compare(0, pre.size(), pre) == 0));
    break;
  }

  case OpCode::STRING_ENDS: {
    BytecodeValue suffix = pop();
    BytecodeValue str = pop();
    if (!str.isStringValId() || !suffix.isStringValId()) {
      throw std::runtime_error("STRING_ENDS expects strings");
    }
    // TODO: string pool lookup
    const std::string s = "<string:" + std::to_string(str.asStringValId()) + ">";
    const std::string suf = "<string:" + std::to_string(suffix.asStringValId()) + ">";
    push(Value::makeBool(s.size() >= suf.size() &&
                       s.compare(s.size() - suf.size(), suf.size(), suf) == 0));
    break;
  }

  // Spread operator - spread array elements
  case OpCode::SPREAD: {
    BytecodeValue value = pop();
    if (value.isArrayId()) {
      auto arrRef = ArrayRef{value.asArrayId()};
      auto *arr = heap_.array(arrRef.id);
      if (arr) {
        // Push each element individually
        for (auto &elem : *arr) {
          push(elem);
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
        push(Value::makeArrayId(arrRef.id));
      }
    }
    break;
  }

  // Spread in function call
  case OpCode::SPREAD_CALL: {
    // Similar to SPREAD but marks arguments for spread in CALL
    BytecodeValue value = pop();
    if (value.isArrayId()) {
      auto arrRef = ArrayRef{value.asArrayId()};
      auto *arr = heap_.array(arrRef.id);
      if (arr) {
        for (auto &elem : *arr) {
          push(elem);
        }
      }
    }
    break;
  }

  // Type conversion - as operator
  case OpCode::AS_TYPE: {
    if (instruction.operands.size() < 1) {
      throw std::runtime_error("AS_TYPE requires type operand");
    }
    const std::string &typeName =
        instruction.operands[0].toString();
    BytecodeValue value = pop();

    if (typeName == "int" || typeName == "Int") {
      push(toInt(value));
    } else if (typeName == "float" || typeName == "Float" ||
               typeName == "double" || typeName == "num" || typeName == "Num") {
      push(toFloat(value));
    } else if (typeName == "string" || typeName == "String") {
      push(toString(value));
    } else if (typeName == "bool" || typeName == "Bool" ||
               typeName == "boolean") {
      push(toBool(value));
    } else if (typeName == "array" || typeName == "Array") {
      // Convert to array if possible
      if (value.isArrayId()) {
        push(value);
      } else {
        auto arrRef = heap_.allocateArray();
        push(Value::makeArrayId(arrRef.id));
      }
    } else {
      push(value); // Unknown type, return as-is
    }
    break;
  }

  // toInt() builtin
  case OpCode::TO_INT: {
    BytecodeValue value = pop();
    push(toInt(value));
    break;
  }

  // toFloat() builtin
  case OpCode::TO_FLOAT: {
    BytecodeValue value = pop();
    push(toFloat(value));
    break;
  }

  // toString() builtin
  case OpCode::TO_STRING: {
    BytecodeValue value = pop();
    push(toString(value));
    break;
  }

  // String concatenation
  case OpCode::STRING_CONCAT: {
    BytecodeValue right = pop();
    BytecodeValue left = pop();
    std::string leftStr = toString(left);
    std::string rightStr = toString(right);
    push(leftStr + rightStr);
    break;
  }

  // toBool() builtin
  case OpCode::TO_BOOL: {
    BytecodeValue value = pop();
    push(toBool(value));
    break;
  }

  // typeof() builtin
  case OpCode::TYPE_OF: {
    BytecodeValue value = pop();
    std::string typeName;
    if (value.isNull()) {
      typeName = "null";
    } else if (value.isBool()) {
      typeName = "bool";
    } else if (value.isInt()) {
      typeName = "int";
    } else if (value.isDouble()) {
      typeName = "num"; // Renamed from "float" to "num" per docs
    } else if (value.isStringValId()) {
      typeName = "string";
    } else if (value.isArrayId()) {
      typeName = "array";
    } else if (value.isObjectId()) {
      typeName = "object";
    } else if (value.isHostFuncId()) {
      typeName = "function";
    } else if (value.isFunctionObjId()) {
      typeName = "function";
    } else if (value.isClosureId()) {
      typeName = "function";
    } else {
      typeName = "unknown";
    }
    push(typeName);
    break;
  }

  case OpCode::PRINT: {
    BytecodeValue value = pop();
    std::cout << toString(value, &heap_) << std::endl;
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
    throw std::runtime_error(
        "Unknown opcode: " +
        std::to_string(static_cast<int>(instruction.opcode)));
  }
}

// ============================================================================
// Callback System - VM owns closures, systems use opaque IDs
// ============================================================================

CallbackId VM::registerCallback(const BytecodeValue &closure) {
  // Accept both ClosureRef and FunctionObject
  if (!closure.isClosureId() && !closure.isFunctionObjId()) {
    throw std::runtime_error("registerCallback expects a closure or function");
  }

  // Pin the closure as an external root (GC will not collect it)
  CallbackId id = static_cast<CallbackId>(pinExternalRoot(closure));

  if (id == INVALID_CALLBACK_ID) {
    throw std::runtime_error("Failed to register callback - invalid ID");
  }

  return id;
}

BytecodeValue VM::invokeCallback(CallbackId id,
                                 const std::vector<BytecodeValue> &args) {
  if (id == INVALID_CALLBACK_ID) {
    throw std::runtime_error("invokeCallback called with invalid callback ID");
  }

  // Get the closure from external roots
  auto closureValue = externalRootValue(id);
  if (!closureValue.has_value()) {
    throw std::runtime_error(
        "invokeCallback: callback not found (may have been released)");
  }

  // Call the closure and return result
  return call(*closureValue,
              std::vector<BytecodeValue>(args.begin(), args.end()));
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
bool VM::isNull(const BytecodeValue &value) const {
  return value.isNull();
}

bool VM::isTruthy(const BytecodeValue &value) {
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
std::optional<int64_t> VM::parseDuration(const BytecodeValue &value) const {
  if (value.isInt()) {
    return value.asInt();
  }

  if (value.isDouble()) {
    return static_cast<int64_t>(value.asDouble());
  }

  if (value.isStringValId()) {
    // TODO: string pool lookup
    const std::string duration_str = "<string:" + std::to_string(value.asStringValId()) + ">";

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

BytecodeValue
VM::VMExecutionContext::invokeCallback(CallbackId id,
                                       const std::vector<BytecodeValue> &args) {
  if (!parent_vm_) {
    throw std::runtime_error(
        "VMExecutionContext::invokeCallback called on invalid context");
  }
  if (id == INVALID_CALLBACK_ID) {
    throw std::runtime_error("invokeCallback called with invalid callback ID");
  }

  // Get the closure from parent's external roots
  auto closureValue = parent_vm_->externalRootValue(id);
  if (!closureValue.has_value()) {
    throw std::runtime_error(
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
      throw std::runtime_error("Closure not found: " +
                               std::to_string(closure_id));
    }
    function_index = closure->function_index;
  } else if (closureValue->isFunctionObjId()) {
    function_index = closureValue->asFunctionObjId();
  } else {
    throw std::runtime_error("Callback must be a closure or function");
  }

  const auto *func = current_chunk->getFunction(function_index);
  if (!func) {
    throw std::runtime_error("Function index not found: " +
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
        throw std::runtime_error("Uncaught exception: " +
                                 toString(thrown.value, &parent_vm_->heap_));
      }
      continue;
    }

    if (frame_count_ > stop_frame_depth) {
      active_frame_idx = frame_count_ - 1;
      if (frame_arena_[active_frame_idx].ip == ip) {
        frame_arena_[active_frame_idx].ip++;
      }
    }
  }

  if (stack.empty()) {
    return nullptr;
  }
  BytecodeValue result = stack.top();
  stack.pop();
  return result;
}

void VM::VMExecutionContext::executeInstructionInContext(
    const Instruction &instruction) {
  // Helper lambdas that operate on THIS context's state (not parent VM)
  auto pop = [this]() -> BytecodeValue {
    if (stack.empty()) {
      throw std::runtime_error("Stack underflow");
    }
    BytecodeValue value = stack.top();
    stack.pop();
    return value;
  };

  auto push = [this](BytecodeValue value) { stack.push(std::move(value)); };

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
      throw std::runtime_error("No active call frame");
    }
    return frame_arena_[frame_count_ - 1];
  };

  auto getConstant = [this](uint32_t index) -> BytecodeValue {
    return frame_arena_[frame_count_ - 1].function->constants[index];
  };

  auto doReturn = [this, &pop, &push]() {
    if (frame_count_ == 0) {
      return;
    }

    BytecodeValue ret = nullptr;
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
    uint32_t const_index = std::get<uint32_t>(instruction.operands[0]);
    push(getConstant(const_index));
    break;
  }

  case OpCode::LOAD_GLOBAL: {
    const auto &name = instruction.operands[0].toString();
    // Thread-safe access to parent's globals
    auto value = parent_vm_->getGlobalThreadSafe(name);
    push(value.value_or(nullptr));
    break;
  }

  case OpCode::STORE_GLOBAL: {
    const auto &name = instruction.operands[0].toString();
    BytecodeValue value = pop();
    parent_vm_->setGlobalThreadSafe(name, std::move(value));
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
    auto *closure = parent_vm_->heap_.closure(closure_id);
    if (!closure || upvalue_index >= closure->upvalues.size() ||
        !closure->upvalues[upvalue_index]) {
      throw std::runtime_error("LOAD_UPVALUE error");
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
    uint32_t upvalue_index = std::get<uint32_t>(instruction.operands[0]);
    uint32_t closure_id = currentFrame().closure_id;
    auto *closure = parent_vm_->heap_.closure(closure_id);
    if (!closure || upvalue_index >= closure->upvalues.size() ||
        !closure->upvalues[upvalue_index]) {
      throw std::runtime_error("STORE_UPVALUE error");
    }
    auto &cell = closure->upvalues[upvalue_index];
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

  case OpCode::PUSH_NULL: {
    push(nullptr);
    break;
  }

  case OpCode::ADD: {
    BytecodeValue right = pop();
    BytecodeValue left = pop();
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
      push(l + r);
    } else {
      throw std::runtime_error("Type mismatch in ADD");
    }
    break;
  }

  case OpCode::SUB: {
    BytecodeValue right = pop();
    BytecodeValue left = pop();
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
    BytecodeValue right = pop();
    BytecodeValue left = pop();
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
    BytecodeValue right = pop();
    BytecodeValue left = pop();
    double l = left.isInt()
                   ? static_cast<double>(left.asInt())
                   : left.asDouble();
    double r = right.isInt()
                   ? static_cast<double>(right.asInt())
                   : right.asDouble();
    if (r == 0)
      throw ScriptThrow{BytecodeValue("Division by zero")};
    push(l / r);
    break;
  }

  case OpCode::EQ: {
    BytecodeValue right = pop();
    BytecodeValue left = pop();
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
    BytecodeValue right = pop();
    BytecodeValue left = pop();
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
    BytecodeValue right = pop();
    BytecodeValue left = pop();
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
    uint32_t target = std::get<uint32_t>(instruction.operands[0]);
    currentFrame().ip = target;
    break;
  }

  case OpCode::JUMP_IF_FALSE: {
    uint32_t target = std::get<uint32_t>(instruction.operands[0]);
    BytecodeValue condition = pop();
    if (!parent_vm_->isTruthy(condition)) {
      currentFrame().ip = target;
    }
    break;
  }

  case OpCode::CALL: {
    uint32_t arg_count = std::get<uint32_t>(instruction.operands[0]);
    std::vector<BytecodeValue> args(arg_count);
    for (uint32_t i = 0; i < arg_count; ++i) {
      args[arg_count - 1 - i] = pop();
    }
    BytecodeValue callee_value = pop();

    // Handle host function call
    if (callee_value.isHostFuncId()) {
      // TODO: host func name lookup
      (void)callee_value.asHostFuncId();
      throw std::runtime_error("Host function call in execution context not yet supported with NaN boxing");
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
        throw std::runtime_error("Closure not found");
      }
      function_index = closure->function_index;
    } else {
      throw std::runtime_error("CALL expects function or closure");
    }

    const auto *callee = current_chunk->getFunction(function_index);
    if (!callee) {
      throw std::runtime_error("Function not found");
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
    uint32_t function_index = std::get<uint32_t>(instruction.operands[0]);
    const auto *target = current_chunk->getFunction(function_index);
    if (!target) {
      throw std::runtime_error("CLOSURE references unknown function index");
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

    push(parent_vm_->heap_.allocateClosure(GCHeap::RuntimeClosure{
        .function_index = function_index, .upvalues = std::move(upvalues)}));
    break;
  }

  case OpCode::CALL_HOST: {
    const std::string &function_name =
        instruction.operands[0].toString();
    uint32_t arg_count = std::get<uint32_t>(instruction.operands[1]);

    std::vector<BytecodeValue> args(arg_count);
    for (uint32_t i = 0; i < arg_count; ++i) {
      args[arg_count - 1 - i] = pop();
    }

    auto it = parent_vm_->host_functions.find(function_name);
    if (it != parent_vm_->host_functions.end()) {
      push(it->second(args));
    } else {
      throw std::runtime_error(parent_vm_->formatErrorWithContext(
          "Host function not found: " + function_name));
    }
    break;
  }

  default:
    throw std::runtime_error(parent_vm_->formatErrorWithContext(
        "Unsupported opcode in execution context: " +
        std::to_string(static_cast<int>(instruction.opcode))));
  }
}

void VM::setGlobalThreadSafe(const std::string &name, BytecodeValue value) {
  std::unique_lock<std::shared_mutex> lock(globals_mutex_);
  globals[name] = std::move(value);
}

std::optional<BytecodeValue>
VM::getGlobalThreadSafe(const std::string &name) const {
  std::shared_lock<std::shared_mutex> lock(globals_mutex_);
  auto it = globals.find(name);
  if (it == globals.end()) {
    return std::nullopt;
  }
  return it->second;
}

} // namespace havel::compiler
