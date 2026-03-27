#include "VM.hpp"

#include "../../../core/io/MouseController.hpp" // For ParseDuration
#include <chrono>
#include <cmath>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace havel::compiler {

namespace {
// Helper function to compare two BytecodeValues for equality
static bool valuesEqual(const BytecodeValue& a, const BytecodeValue& b) {
  // Compare variant indices first for type check
  if (a.index() != b.index()) return false;
  
  if (std::holds_alternative<std::nullptr_t>(a)) return true;
  if (std::holds_alternative<bool>(a)) return std::get<bool>(a) == std::get<bool>(b);
  if (std::holds_alternative<int64_t>(a)) return std::get<int64_t>(a) == std::get<int64_t>(b);
  if (std::holds_alternative<double>(a)) return std::get<double>(a) == std::get<double>(b);
  if (std::holds_alternative<std::string>(a)) return std::get<std::string>(a) == std::get<std::string>(b);
  
  // For reference types, compare IDs
  if (std::holds_alternative<ArrayRef>(a)) {
    return std::get<ArrayRef>(a).id == std::get<ArrayRef>(b).id;
  }
  if (std::holds_alternative<ObjectRef>(a)) {
    return std::get<ObjectRef>(a).id == std::get<ObjectRef>(b).id;
  }
  if (std::holds_alternative<RangeRef>(a)) {
    return std::get<RangeRef>(a).id == std::get<RangeRef>(b).id;
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
    if (!heap) {
      return "array[" + std::to_string(std::get<ArrayRef>(value).id) + "]";
    }
    auto *arr = heap->array(std::get<ArrayRef>(value).id);
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
  if (std::holds_alternative<ObjectRef>(value)) {
    if (!heap) {
      return "object[" + std::to_string(std::get<ObjectRef>(value).id) + "]";
    }
    auto *obj = heap->object(std::get<ObjectRef>(value).id);
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
  if (std::holds_alternative<SetRef>(value)) {
    return "set[" + std::to_string(std::get<SetRef>(value).id) + "]";
  }
  if (std::holds_alternative<HostFunctionRef>(value)) {
    return "hostfn[" + std::get<HostFunctionRef>(value).name + "]";
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
  if (std::holds_alternative<int64_t>(value)) {
    return std::get<int64_t>(value);
  }
  if (std::holds_alternative<double>(value)) {
    return static_cast<int64_t>(std::get<double>(value));
  }
  if (std::holds_alternative<bool>(value)) {
    return std::get<bool>(value) ? 1 : 0;
  }
  if (std::holds_alternative<std::string>(value)) {
    try {
      return std::stoll(std::get<std::string>(value));
    } catch (...) {
      return 0;
    }
  }
  return 0;
}

double toFloat(const BytecodeValue &value) {
  if (std::holds_alternative<double>(value)) {
    return std::get<double>(value);
  }
  if (std::holds_alternative<int64_t>(value)) {
    return static_cast<double>(std::get<int64_t>(value));
  }
  if (std::holds_alternative<bool>(value)) {
    return std::get<bool>(value) ? 1.0 : 0.0;
  }
  if (std::holds_alternative<std::string>(value)) {
    try {
      return std::stod(std::get<std::string>(value));
    } catch (...) {
      return 0.0;
    }
  }
  return 0.0;
}

bool toBool(const BytecodeValue &value) {
  if (std::holds_alternative<bool>(value)) {
    return std::get<bool>(value);
  }
  if (std::holds_alternative<int64_t>(value)) {
    return std::get<int64_t>(value) != 0;
  }
  if (std::holds_alternative<double>(value)) {
    return std::get<double>(value) != 0.0;
  }
  if (std::holds_alternative<std::string>(value)) {
    return !std::get<std::string>(value).empty();
  }
  // Collections: JavaScript truthiness (all collections are truthy, even empty)
  // This matches: if (arr) { ... } pattern
  if (std::holds_alternative<ArrayRef>(value)) {
    return true;
  }
  if (std::holds_alternative<ObjectRef>(value)) {
    return true;
  }
  if (std::holds_alternative<SetRef>(value)) {
    return true;
  }
  return false; // null, undefined, etc.
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

VM::VM() { registerDefaultHostFunctions(); }

VM::VM(const havel::HostContext &ctx) {
  // Store context for service access
  context_ = &ctx;
  registerDefaultHostFunctions();
}

void VM::setMaxCallDepth(size_t value) {
  max_call_depth_ = value;
}

VM::~VM() {
  if (heap_.externalRootCount() > 0) {
    std::cerr << "[VM][GC] Warning: " << heap_.externalRootCount()
              << " external roots still pinned at VM shutdown" << std::endl;
  }
}

template <typename T> T VM::getValue(const BytecodeValue &value) {
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

void VM::scheduleCall(const BytecodeValue &fn, const std::vector<BytecodeValue> &args, BytecodeValue &result, bool &completed) {
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
BytecodeValue VM::callFunctionSync(const BytecodeValue &fn, const std::vector<BytecodeValue> &args) {
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
    return BytecodeValue(nullptr);
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
      array->push_back(BytecodeValue(nullptr));
    }
  }
  (*array)[index] = std::move(value);
}

BytecodeValue VM::popHostArrayValue(ArrayRef array_ref) {
  auto *array = heap_.array(array_ref.id);
  if (!array || array->empty())
    return BytecodeValue(nullptr);
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
    return BytecodeValue(nullptr);
  auto value = std::move((*array)[index]);
  array->erase(array->begin() + index);
  return value;
}

// Range helpers
bool VM::isInRange(RangeRef range_ref, int64_t value) {
  auto *r = heap_.range(range_ref.id);
  if (!r) return false;
  
  if (r->step > 0) {
    return value >= r->start && value < r->end && (value - r->start) % r->step == 0;
  } else {
    return value <= r->start && value > r->end && (r->start - value) % (-r->step) == 0;
  }
}

// Struct helpers
uint32_t VM::registerStructType(const std::string& name, const std::vector<std::string>& fields) {
  return heap_.registerStructType(name, fields);
}

StructRef VM::createStruct(uint32_t typeId, size_t fieldCount) {
  return heap_.allocateStruct(typeId, fieldCount);
}

BytecodeValue VM::getStructField(StructRef struct_ref, size_t index) {
  auto it = heap_.structs_.find(struct_ref.id);
  if (it == heap_.structs_.end() || index >= it->second.size()) {
    return BytecodeValue(nullptr);
  }
  return it->second[index];
}

void VM::setStructField(StructRef struct_ref, size_t index, const BytecodeValue& value) {
  auto it = heap_.structs_.find(struct_ref.id);
  if (it == heap_.structs_.end() || index >= it->second.size()) {
    return;
  }
  it->second[index] = value;
}

uint32_t VM::getStructTypeId(StructRef struct_ref) {
  return struct_ref.typeId;
}

// Enum helpers
uint32_t VM::registerEnumType(const std::string& name, const std::vector<std::string>& variants) {
  return heap_.registerEnumType(name, variants);
}

EnumRef VM::createEnum(uint32_t typeId, uint32_t tag, size_t payloadCount) {
  return heap_.allocateEnum(typeId, tag, payloadCount);
}

uint32_t VM::getEnumTag(EnumRef enum_ref) {
  return enum_ref.tag;
}

BytecodeValue VM::getEnumPayload(EnumRef enum_ref, size_t index) {
  auto it = heap_.enums_.find(enum_ref.id);
  if (it == heap_.enums_.end() || index >= it->second.second.size()) {
    return BytecodeValue(nullptr);
  }
  return it->second.second[index];
}

void VM::setEnumPayload(EnumRef enum_ref, size_t index, const BytecodeValue& value) {
  auto it = heap_.enums_.find(enum_ref.id);
  if (it == heap_.enums_.end() || index >= it->second.second.size()) {
    return;
  }
  it->second.second[index] = value;
}

// Membership helpers
bool VM::arrayContains(ArrayRef array_ref, const BytecodeValue& value) {
  auto *array = heap_.array(array_ref.id);
  if (!array) return false;
  for (const auto& item : *array) {
    if (valuesEqual(item, value)) {
      return true;
    }
  }
  return false;
}

bool VM::objectHasKey(ObjectRef object_ref, const std::string& key) {
  auto *object = heap_.object(object_ref.id);
  if (!object) return false;
  return object->find(key) != object->end();
}

// Iterator helpers
IteratorRef VM::createIterator(const BytecodeValue& iterable) {
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

BytecodeValue VM::getHostObjectField(ObjectRef object_ref, const std::string &key) {
  auto *object = heap_.object(object_ref.id);
  if (!object)
    return BytecodeValue(nullptr);
  auto it = object->find(key);
  if (it == object->end())
    return BytecodeValue(nullptr);
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
  if (std::holds_alternative<HostFunctionRef>(fn)) {
    auto hostFnRef = std::get<HostFunctionRef>(fn);
    auto it = host_functions.find(hostFnRef.name);
    if (it != host_functions.end()) {
      return it->second(args);
    }
  }
  return BytecodeValue(nullptr);
}

// General function call (handles both VM closures and host functions)
BytecodeValue VM::callFunction(const BytecodeValue &fn,
                               const std::vector<BytecodeValue> &args) {
  // Host function - direct call
  if (std::holds_alternative<HostFunctionRef>(fn)) {
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
  if (std::holds_alternative<std::string>(value)) {
    typeName = "String";
  } else if (std::holds_alternative<ArrayRef>(value)) {
    typeName = "Array";
  } else if (std::holds_alternative<ObjectRef>(value)) {
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
  if (std::holds_alternative<std::string>(value)) {
    typeName = "String";
  } else if (std::holds_alternative<ArrayRef>(value)) {
    typeName = "Array";
  } else if (std::holds_alternative<ObjectRef>(value)) {
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
    if (!args.empty() && std::holds_alternative<ObjectRef>(args.back())) {
      auto *kwargsObj = heap_.object(std::get<ObjectRef>(args.back()).id);
      if (kwargsObj) {
        auto itEnd = kwargsObj->find("end");
        if (itEnd != kwargsObj->end() && std::holds_alternative<std::string>(itEnd->second)) {
          end = std::get<std::string>(itEnd->second);
        }
        auto itDelim = kwargsObj->find("delim");
        if (itDelim != kwargsObj->end() && std::holds_alternative<std::string>(itDelim->second)) {
          delim = std::get<std::string>(itDelim->second);
        }
        argCount--;  // Don't count kwargs as a value to print
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
    return BytecodeValue(nullptr);
  });
  // Also add to globals for closure access
  setGlobal("print", BytecodeValue(HostFunctionRef{"print"}));

  // fmt(format_string, ...) - Python-style string formatting
  registerHostFunction("fmt", [this](const std::vector<BytecodeValue> &args) {
    if (args.empty()) {
      throw std::runtime_error("fmt() requires at least a format string");
    }
    
    // Get format string
    if (!std::holds_alternative<std::string>(args[0])) {
      throw std::runtime_error("fmt() format must be a string");
    }
    std::string formatStr = std::get<std::string>(args[0]);
    
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
        result += "{}";  // No more args, keep placeholder
      }
      
      pos = placeholder + 2;  // Skip past {}
    }
    
    return BytecodeValue(result);
  });

  registerHostFunction("clock_ms", 0, [](const std::vector<BytecodeValue> &) {
    const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now())
                         .time_since_epoch()
                         .count();
    return BytecodeValue(static_cast<int64_t>(now));
  });

  registerHostFunction(
      "sleep_ms", 1, [](const std::vector<BytecodeValue> &args) {
        if (!std::holds_alternative<int64_t>(args[0])) {
          throw std::runtime_error(
              "sleep_ms expects exactly 1 integer argument");
        }

        int64_t duration_ms = std::get<int64_t>(args[0]);
        if (duration_ms < 0) {
          throw std::runtime_error("sleep_ms duration cannot be negative");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
        return BytecodeValue(nullptr);
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
        return BytecodeValue(nullptr);
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
      throw std::runtime_error("assert() requires at least a condition argument");
    }
    bool condition = false;
    if (std::holds_alternative<bool>(args[0])) {
      condition = std::get<bool>(args[0]);
    } else if (std::holds_alternative<int64_t>(args[0])) {
      condition = std::get<int64_t>(args[0]) != 0;
    }
    if (!condition) {
      std::string msg = "Assertion failed";
      if (args.size() > 1 && std::holds_alternative<std::string>(args[1])) {
        msg = std::get<std::string>(args[1]);
      }
      throw std::runtime_error(msg);
    }
    return BytecodeValue(nullptr);
  });

  // Performance: clock_ns() - high-resolution clock in nanoseconds
  registerHostFunction("clock_ns", 0, [](const std::vector<BytecodeValue> &) {
    const auto now = std::chrono::time_point_cast<std::chrono::nanoseconds>(
                         std::chrono::steady_clock::now())
                         .time_since_epoch()
                         .count();
    return BytecodeValue(static_cast<int64_t>(now));
  });

  // Performance: clock_us() - clock in microseconds
  registerHostFunction("clock_us", 0, [](const std::vector<BytecodeValue> &) {
    const auto now = std::chrono::time_point_cast<std::chrono::microseconds>(
                         std::chrono::steady_clock::now())
                         .time_since_epoch()
                         .count();
    return BytecodeValue(static_cast<int64_t>(now));
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
    if (std::holds_alternative<std::nullptr_t>(value)) {
      typeName = "null";
    } else if (std::holds_alternative<bool>(value)) {
      typeName = "bool";
    } else if (std::holds_alternative<int64_t>(value)) {
      typeName = "int";
    } else if (std::holds_alternative<double>(value)) {
      typeName = "float";
    } else if (std::holds_alternative<std::string>(value)) {
      typeName = "string";
    } else if (std::holds_alternative<ArrayRef>(value)) {
      typeName = "array";
    } else if (std::holds_alternative<ObjectRef>(value)) {
      typeName = "object";
    } else if (std::holds_alternative<HostFunctionRef>(value)) {
      typeName = "function";
    } else {
      typeName = "unknown";
    }
    return BytecodeValue(typeName);
  });

  auto registerSystemGc = [this](const std::string &name) {
    registerHostFunction(name, 0, [this](const std::vector<BytecodeValue> &) {
      runGarbageCollection();
      return BytecodeValue(nullptr);
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
        if (args.size() != 2 || !std::holds_alternative<std::string>(args[0]) ||
            !std::holds_alternative<ArrayRef>(args[1])) {
          throw std::runtime_error(
              "struct.define(name, fields) expects (string, array)");
        }
        const auto &name = std::get<std::string>(args[0]);
        auto *field_array = heap_.array(std::get<ArrayRef>(args[1]).id);
        if (!field_array) {
          throw std::runtime_error("struct.define received invalid fields array");
        }
        std::vector<std::string> fields;
        fields.reserve(field_array->size());
        for (const auto &value : *field_array) {
          if (!std::holds_alternative<std::string>(value)) {
            throw std::runtime_error(
                "struct.define fields must contain only strings");
          }
          fields.push_back(std::get<std::string>(value));
        }
        uint32_t type_id = registerStructType(name, fields);
        struct_type_ids_by_name_[name] = type_id;
        return BytecodeValue(static_cast<int64_t>(type_id));
      });

  registerHostFunction(
      "struct.new", [this](const std::vector<BytecodeValue> &args) {
        if (args.empty()) {
          throw std::runtime_error(
              "struct.new(type, ...values) requires a type argument");
        }
        uint32_t type_id = 0;
        if (std::holds_alternative<int64_t>(args[0])) {
          type_id = static_cast<uint32_t>(std::get<int64_t>(args[0]));
        } else if (std::holds_alternative<std::string>(args[0])) {
          const auto &name = std::get<std::string>(args[0]);
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
        if (args.size() != 2 || !std::holds_alternative<StructRef>(args[0])) {
          throw std::runtime_error("struct.get(struct, field) expects struct");
        }
        StructRef ref = std::get<StructRef>(args[0]);
        size_t index = 0;
        if (std::holds_alternative<int64_t>(args[1])) {
          index = static_cast<size_t>(std::get<int64_t>(args[1]));
        } else if (std::holds_alternative<std::string>(args[1])) {
          auto idx = heap_.structFieldIndex(ref.typeId, std::get<std::string>(args[1]));
          if (!idx.has_value()) {
            return BytecodeValue(nullptr);
          }
          index = *idx;
        } else {
          throw std::runtime_error("struct.get field must be string or int");
        }
        return getStructField(ref, index);
      });

  registerHostFunction(
      "struct.set", [this](const std::vector<BytecodeValue> &args) {
        if (args.size() != 3 || !std::holds_alternative<StructRef>(args[0])) {
          throw std::runtime_error(
              "struct.set(struct, field, value) expects struct");
        }
        StructRef ref = std::get<StructRef>(args[0]);
        size_t index = 0;
        if (std::holds_alternative<int64_t>(args[1])) {
          index = static_cast<size_t>(std::get<int64_t>(args[1]));
        } else if (std::holds_alternative<std::string>(args[1])) {
          auto idx = heap_.structFieldIndex(ref.typeId, std::get<std::string>(args[1]));
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
}

void VM::registerDefaultHostGlobals() {
  auto system_obj = heap_.allocateObject();
  setHostObjectField(system_obj, "gc", HostFunctionRef{.name = "system.gc"});
  setHostObjectField(system_obj, "gcStats",
                     HostFunctionRef{.name = "system.gcStats"});
  setGlobal("system", system_obj);

  auto struct_obj = heap_.allocateObject();
  setHostObjectField(struct_obj, "define", HostFunctionRef{.name = "struct.define"});
  setHostObjectField(struct_obj, "new", HostFunctionRef{.name = "struct.new"});
  setHostObjectField(struct_obj, "get", HostFunctionRef{.name = "struct.get"});
  setHostObjectField(struct_obj, "set", HostFunctionRef{.name = "struct.set"});
  setGlobal("struct", struct_obj);
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

void VM::runDispatchLoop(size_t stop_frame_depth) {
  while (frame_count_ > stop_frame_depth) {
    // CRITICAL: Capture ALL frame data by value BEFORE any mutation!
    // doCall() may cause vector reallocation, invalidating all references/indices.
    size_t active_frame_idx = frame_count_ - 1;
    
    // Capture frame data by value - do NOT keep references!
    const auto* function = frame_arena_[active_frame_idx].function;
    uint32_t ip = frame_arena_[active_frame_idx].ip;
    uint32_t previous_ip = ip;

    if (ip >= function->instructions.size()) {
      stack.push(nullptr);
      executeInstruction(Instruction{OpCode::RETURN});
      continue;
    }

    const auto& instruction = function->instructions[ip];

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
        throw std::runtime_error("Uncaught exception: " +
                                 toString(thrown.value, &heap_));
      }
      continue;
    }

    processPendingCalls();

    // CRITICAL: Re-fetch frame AFTER executeInstruction (vector may have reallocated)
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

      // Jump to catch block (finally is compiled into the catch block if it exists)
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
  if (std::holds_alternative<HostFunctionRef>(callee_value)) {
    const auto &name = std::get<HostFunctionRef>(callee_value).name;
    auto it = host_functions.find(name);
    if (it == host_functions.end()) {
      throw std::runtime_error("Host function not found: " + name);
    }
    stack.push(it->second(args));
    return;
  }

  if (frame_count_ >= max_call_depth_) {
    throw std::runtime_error("Stack overflow: maximum call depth " +
                             std::to_string(max_call_depth_) + " reached");
  }

  // Handle host function call
  if (std::holds_alternative<HostFunctionRef>(callee_value)) {
    const auto &name = std::get<HostFunctionRef>(callee_value).name;
    auto it = host_functions.find(name);
    if (it == host_functions.end()) {
      throw std::runtime_error("Host function not found: " + name);
    }
    stack.push(it->second(args));
    return;
  }

  uint32_t function_index = 0;
  uint32_t closure_id = 0;
  if (std::holds_alternative<FunctionObject>(callee_value)) {
    function_index = std::get<FunctionObject>(callee_value).function_index;
  } else if (std::holds_alternative<ClosureRef>(callee_value)) {
    closure_id = std::get<ClosureRef>(callee_value).id;
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
  if (std::holds_alternative<FunctionObject>(callee_value)) {
    // Check if function is valid
  }

  // Allow fewer arguments than parameters (for default parameters)
  // For variadic functions, allow MORE arguments than parameters
  if (callee->variadic_param_index == UINT32_MAX && args.size() > callee->param_count) {
    throw std::runtime_error("Argument count mismatch calling function index " +
                             std::to_string(function_index) + " (expected at most " +
                             std::to_string(callee->param_count) + ", got " +
                             std::to_string(args.size()) + ")");
  }
  
  // For variadic functions, require at least as many args as non-variadic params
  if (callee->variadic_param_index != UINT32_MAX && args.size() < callee->variadic_param_index) {
    throw std::runtime_error("Argument count mismatch calling function index " +
                             std::to_string(function_index) + " (expected at least " +
                             std::to_string(callee->variadic_param_index) + ", got " +
                             std::to_string(args.size()) + ")");
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
    if (callee->variadic_param_index != UINT32_MAX && i == callee->variadic_param_index) {
      // Variadic parameter: pack remaining args into array
      auto arrRef = heap_.allocateArray();
      auto *arr = heap_.array(arrRef.id);
      for (size_t j = i; j < args.size(); j++) {
        arr->push_back(std::move(args[j]));
      }
      locals[base + i] = BytecodeValue(arrRef);
    } else if (i < args.size()) {
      locals[base + i] = std::move(args[i]);
    } else if (i < callee->default_values.size() && callee->default_values[i].has_value()) {
      locals[base + i] = callee->default_values[i].value();
    } else {
      locals[base + i] = nullptr;  // No arg provided, no default
    }
  }
}

void VM::doTailCall(BytecodeValue callee_value, std::vector<BytecodeValue> args) {
  // Tail call optimization: reuse current frame instead of pushing new one
  if (std::holds_alternative<HostFunctionRef>(callee_value)) {
    const auto &name = std::get<HostFunctionRef>(callee_value).name;
    auto it = host_functions.find(name);
    if (it == host_functions.end()) {
      throw std::runtime_error("Host function not found: " + name);
    }
    // For host functions, just call and push result (no frame manipulation needed)
    stack.push(it->second(args));
    return;
  }

  uint32_t function_index = 0;
  uint32_t closure_id = 0;
  if (std::holds_alternative<FunctionObject>(callee_value)) {
    function_index = std::get<FunctionObject>(callee_value).function_index;
  } else if (std::holds_alternative<ClosureRef>(callee_value)) {
    closure_id = std::get<ClosureRef>(callee_value).id;
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
  if (callee->variadic_param_index == UINT32_MAX && args.size() > callee->param_count) {
    throw std::runtime_error("Argument count mismatch for tail call to function index " +
                             std::to_string(function_index) + " (expected at most " +
                             std::to_string(callee->param_count) + ", got " +
                             std::to_string(args.size()) + ")");
  }
  
  // For variadic functions, require at least as many args as non-variadic params
  if (callee->variadic_param_index != UINT32_MAX && args.size() < callee->variadic_param_index) {
    throw std::runtime_error("Argument count mismatch for tail call to function index " +
                             std::to_string(function_index) + " (expected at least " +
                             std::to_string(callee->variadic_param_index) + ", got " +
                             std::to_string(args.size()) + ")");
  }

  // TCO: Reuse current frame - update function, reset IP, adjust locals
  auto& current_frame = currentFrame();
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

  // Set up arguments in the reused frame (at old_base): provided args first, then defaults
  // Handle variadic parameters: pack extra args into array
  for (uint32_t i = 0; i < callee->param_count; i++) {
    if (callee->variadic_param_index != UINT32_MAX && i == callee->variadic_param_index) {
      // Variadic parameter: pack remaining args into array
      auto arrRef = heap_.allocateArray();
      auto *arr = heap_.array(arrRef.id);
      for (size_t j = i; j < args.size(); j++) {
        arr->push_back(std::move(args[j]));
      }
      locals[old_base + i] = BytecodeValue(arrRef);
    } else if (i < args.size()) {
      locals[old_base + i] = std::move(args[i]);
    } else if (i < callee->default_values.size() && callee->default_values[i].has_value()) {
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
        !std::holds_alternative<std::string>(instruction.operands[0])) {
      throw std::runtime_error("LOAD_GLOBAL expects string operand");
    }
    const auto &name = std::get<std::string>(instruction.operands[0]);
    auto it = globals.find(name);
    push(it == globals.end() ? BytecodeValue(nullptr) : it->second);
    break;
  }

  case OpCode::STORE_GLOBAL: {
    if (instruction.operands.empty() ||
        !std::holds_alternative<std::string>(instruction.operands[0])) {
      throw std::runtime_error("STORE_GLOBAL expects string operand");
    }
    const auto &name = std::get<std::string>(instruction.operands[0]);
    globals[name] = pop();
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
    push(BytecodeValue(nullptr));
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
        push(static_cast<int64_t>(std::pow(static_cast<double>(l), static_cast<double>(r))));
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
    bool isNull = std::holds_alternative<std::nullptr_t>(value);
    push(BytecodeValue(isNull));
    break;
  }

  case OpCode::JUMP_IF_NULL: {
    uint32_t target = std::get<uint32_t>(instruction.operands[0]);
    BytecodeValue value = pop();
    // Only jump on null/undefined, not on all falsy values
    if (std::holds_alternative<std::nullptr_t>(value)) {
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
        !std::holds_alternative<std::string>(instruction.operands[0]) ||
        !std::holds_alternative<uint32_t>(instruction.operands[1])) {
      throw std::runtime_error("CALL_HOST expects operands: <string "
                               "function_name, uint32 arg_count>");
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
    currentFrame().try_stack.push_back(
        TryHandler{.catch_ip = catch_ip, .finally_ip = finally_ip, .finally_return_ip = 0, .stack_depth = stack.size()});
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
    if (!std::holds_alternative<ArrayRef>(container)) {
      throw std::runtime_error("ARRAY_PUSH expects array container");
    }
    uint32_t id = std::get<ArrayRef>(container).id;
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
    if (!std::holds_alternative<ArrayRef>(container)) {
      throw std::runtime_error("ARRAY_LEN expects array container");
    }
    uint32_t id = std::get<ArrayRef>(container).id;
    auto *array = heap_.array(id);
    if (!array) {
      throw std::runtime_error("ARRAY_LEN unknown array id");
    }
    push(BytecodeValue(static_cast<int64_t>(array->size())));
    break;
  }

  // Range creation: start..end or start..step..end
  case OpCode::RANGE_NEW: {
    int64_t end = std::get<int64_t>(pop());
    int64_t start = std::get<int64_t>(pop());
    RangeRef rangeRef = heap_.allocateRange(start, end, 1);
    push(BytecodeValue(rangeRef));
    break;
  }

  case OpCode::RANGE_STEP_NEW: {
    int64_t step = std::get<int64_t>(pop());
    int64_t end = std::get<int64_t>(pop());
    int64_t start = std::get<int64_t>(pop());
    RangeRef rangeRef = heap_.allocateRange(start, end, step);
    push(BytecodeValue(rangeRef));
    break;
  }

  // Struct operations
  case OpCode::STRUCT_NEW: {
    // Operands: typeId (uint32), fieldCount (uint32)
    uint32_t typeId = std::get<uint32_t>(instruction.operands[0]);
    uint32_t fieldCount = std::get<uint32_t>(instruction.operands[1]);
    StructRef structRef = heap_.allocateStruct(typeId, fieldCount);
    push(BytecodeValue(structRef));
    break;
  }

  case OpCode::STRUCT_GET: {
    // Pop: struct ref, index
    BytecodeValue indexVal = pop();
    BytecodeValue structVal = pop();
    if (!std::holds_alternative<StructRef>(structVal) || !std::holds_alternative<int64_t>(indexVal)) {
      throw std::runtime_error("STRUCT_GET expects struct and int index");
    }
    auto structRef = std::get<StructRef>(structVal);
    size_t index = static_cast<size_t>(std::get<int64_t>(indexVal));
    push(heap_.structs_.at(structRef.id).at(index));
    break;
  }

  case OpCode::STRUCT_SET: {
    // Pop: struct ref, index, value
    BytecodeValue value = pop();
    BytecodeValue indexVal = pop();
    BytecodeValue structVal = pop();
    if (!std::holds_alternative<StructRef>(structVal) || !std::holds_alternative<int64_t>(indexVal)) {
      throw std::runtime_error("STRUCT_SET expects struct, int index, and value");
    }
    auto structRef = std::get<StructRef>(structVal);
    size_t index = static_cast<size_t>(std::get<int64_t>(indexVal));
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
    push(BytecodeValue(enumRef));
    break;
  }

  case OpCode::ENUM_TAG: {
    BytecodeValue enumVal = pop();
    if (!std::holds_alternative<EnumRef>(enumVal)) {
      throw std::runtime_error("ENUM_TAG expects enum");
    }
    auto enumRef = std::get<EnumRef>(enumVal);
    push(BytecodeValue(static_cast<int64_t>(enumRef.tag)));
    break;
  }

  case OpCode::ENUM_PAYLOAD: {
    BytecodeValue indexVal = pop();
    BytecodeValue enumVal = pop();
    if (!std::holds_alternative<EnumRef>(enumVal) || !std::holds_alternative<int64_t>(indexVal)) {
      throw std::runtime_error("ENUM_PAYLOAD expects enum and int index");
    }
    auto enumRef = std::get<EnumRef>(enumVal);
    size_t index = static_cast<size_t>(std::get<int64_t>(indexVal));
    push(heap_.enums_.at(enumRef.id).second.at(index));
    break;
  }

  case OpCode::ENUM_MATCH: {
    // Pop: enum ref, expected tag
    BytecodeValue tagVal = pop();
    BytecodeValue enumVal = pop();
    if (!std::holds_alternative<EnumRef>(enumVal) || !std::holds_alternative<int64_t>(tagVal)) {
      throw std::runtime_error("ENUM_MATCH expects enum and int tag");
    }
    auto enumRef = std::get<EnumRef>(enumVal);
    int64_t expectedTag = std::get<int64_t>(tagVal);
    push(BytecodeValue(enumRef.tag == static_cast<uint32_t>(expectedTag)));
    break;
  }

  // Iteration protocol: iter(obj) → iterator
  case OpCode::ITER_NEW: {
    BytecodeValue iterable = pop();
    
    // Create iterator based on type
    IteratorRef iterRef;
    iterRef.id = heap_.createIterator(iterable);
    push(BytecodeValue(iterRef));
    break;
  }

  // Iteration protocol: iterator.next() → {value, done}
  case OpCode::ITER_NEXT: {
    BytecodeValue iterator_val = pop();
    if (!std::holds_alternative<IteratorRef>(iterator_val)) {
      throw std::runtime_error("ITER_NEXT expects iterator");
    }
    
    uint32_t id = std::get<IteratorRef>(iterator_val).id;
    auto result = heap_.iteratorNext(id);
    
    // result is {value, done} object
    push(result);
    break;
  }

  case OpCode::ARRAY_GET: {
    BytecodeValue index_or_key = pop();
    BytecodeValue container = pop();

    if (std::holds_alternative<ArrayRef>(container)) {
      auto index = indexFromValue(index_or_key);
      if (!index || *index < 0) {
        throw std::runtime_error(
            "ARRAY_GET expects non-negative integer index");
      }
      auto *array = heap_.array(std::get<ArrayRef>(container).id);
      if (!array) {
        throw std::runtime_error("ARRAY_GET unknown array id");
      }
      if (static_cast<size_t>(*index) >= array->size()) {
        push(nullptr);
      } else {
        push((*array)[static_cast<size_t>(*index)]);
      }
      break;
    }

    if (std::holds_alternative<SetRef>(container)) {
      auto key = keyFromValue(index_or_key);
      if (!key) {
        throw std::runtime_error(
            "SET membership expects string/number/bool key");
      }
      auto *set = heap_.set(std::get<SetRef>(container).id);
      if (!set) {
        throw std::runtime_error("ARRAY_GET unknown set id");
      }
      push(set->find(*key) != set->end());
      break;
    }

    if (std::holds_alternative<ObjectRef>(container)) {
      auto key = keyFromValue(index_or_key);
      if (!key) {
        throw std::runtime_error("OBJECT index expects string/number/bool key");
      }
      auto *object = heap_.object(std::get<ObjectRef>(container).id);
      if (!object) {
        throw std::runtime_error("ARRAY_GET unknown object id");
      }
      auto kv = object->find(*key);
      push(kv == object->end() ? BytecodeValue(nullptr) : kv->second);
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
        throw std::runtime_error(
            "ARRAY_SET expects non-negative integer index");
      }
      auto *array = heap_.array(std::get<ArrayRef>(container).id);
      if (!array) {
        throw std::runtime_error("ARRAY_SET unknown array id");
      }
      const auto idx = static_cast<size_t>(*index);
      if (idx >= array->size()) {
        array->resize(idx + 1, nullptr);
      }
      (*array)[idx] = value;
      break;
    }

    if (std::holds_alternative<SetRef>(container)) {
      auto key = keyFromValue(index_or_key);
      if (!key) {
        throw std::runtime_error(
            "SET assignment expects string/number/bool key");
      }
      auto *set = heap_.set(std::get<SetRef>(container).id);
      if (!set) {
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
        (*set)[*key] = nullptr;
      } else {
        set->erase(*key);
      }
      break;
    }

    if (std::holds_alternative<ObjectRef>(container)) {
      auto key = keyFromValue(index_or_key);
      if (!key) {
        throw std::runtime_error("OBJECT index assignment expects valid key");
      }
      auto *object = heap_.object(std::get<ObjectRef>(container).id);
      if (!object) {
        throw std::runtime_error("ARRAY_SET unknown object id");
      }
      (*object)[*key] = value;
      break;
    }

    throw std::runtime_error("ARRAY_SET expects array/set/object container");
  }

  case OpCode::OBJECT_NEW: {
    push(heap_.allocateObject());
    maybeCollectGarbage();
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
    auto *obj = heap_.object(std::get<ObjectRef>(object).id);
    if (!obj) {
      throw std::runtime_error("OBJECT_GET unknown object id");
    }
    auto kv = obj->find(*key);
    push(kv == obj->end() ? BytecodeValue(nullptr) : kv->second);
    break;
  }

  case OpCode::OBJECT_SET: {
    // Stack: [..., obj, value, key] → pops all, pushes obj
    // This allows chaining: obj { DUP, val1, "k1", SET, val2, "k2", SET, ... }
    BytecodeValue key = pop();
    BytecodeValue value = pop();
    BytecodeValue object = pop();
    
    if (!std::holds_alternative<ObjectRef>(object)) {
      throw std::runtime_error("OBJECT_SET expects object container");
    }
    auto keyStr = keyFromValue(key);
    if (!keyStr) {
      throw std::runtime_error("OBJECT_SET expects string/number/bool key");
    }
    
    // Safety: reject __ prefixed keys (reserved for internal use)
    if (keyStr->size() >= 2 && (*keyStr)[0] == '_' && (*keyStr)[1] == '_') {
      throw std::runtime_error("OBJECT_SET: keys starting with '__' are reserved");
    }
    
    auto *obj = heap_.object(std::get<ObjectRef>(object).id);
    if (!obj) {
      throw std::runtime_error("OBJECT_SET unknown object id");
    }
    (*obj)[*keyStr] = value;
    push(object);  // Return the object for chaining
    break;
  }

  // Object intrinsics (VM-level operations)
  case OpCode::OBJECT_KEYS: {
    BytecodeValue object = pop();
    if (!std::holds_alternative<ObjectRef>(object)) {
      throw std::runtime_error("OBJECT_KEYS expects object");
    }
    auto *obj = heap_.object(std::get<ObjectRef>(object).id);
    if (!obj) {
      throw std::runtime_error("OBJECT_KEYS unknown object id");
    }
    auto arrRef = heap_.allocateArray();
    auto *arr = heap_.array(arrRef.id);
    for (const auto& [key, _] : *obj) {
      arr->push_back(BytecodeValue(key));
    }
    push(BytecodeValue(arrRef));
    break;
  }

  case OpCode::OBJECT_VALUES: {
    BytecodeValue object = pop();
    if (!std::holds_alternative<ObjectRef>(object)) {
      throw std::runtime_error("OBJECT_VALUES expects object");
    }
    auto *obj = heap_.object(std::get<ObjectRef>(object).id);
    if (!obj) {
      throw std::runtime_error("OBJECT_VALUES unknown object id");
    }
    auto arrRef = heap_.allocateArray();
    auto *arr = heap_.array(arrRef.id);
    for (const auto& [_, value] : *obj) {
      arr->push_back(value);
    }
    push(BytecodeValue(arrRef));
    break;
  }

  case OpCode::OBJECT_ENTRIES: {
    BytecodeValue object = pop();
    if (!std::holds_alternative<ObjectRef>(object)) {
      throw std::runtime_error("OBJECT_ENTRIES expects object");
    }
    auto *obj = heap_.object(std::get<ObjectRef>(object).id);
    if (!obj) {
      throw std::runtime_error("OBJECT_ENTRIES unknown object id");
    }
    auto arrRef = heap_.allocateArray();
    for (const auto& [key, value] : *obj) {
      // Create [key, value] tuple as array
      auto tupleRef = heap_.allocateArray();
      auto *tuple = heap_.array(tupleRef.id);
      tuple->push_back(BytecodeValue(key));
      tuple->push_back(value);
      auto *arr = heap_.array(arrRef.id);
      arr->push_back(BytecodeValue(tupleRef));
    }
    push(BytecodeValue(arrRef));
    break;
  }

  case OpCode::OBJECT_HAS: {
    BytecodeValue keyValue = pop();
    BytecodeValue object = pop();
    if (!std::holds_alternative<ObjectRef>(object)) {
      throw std::runtime_error("OBJECT_HAS expects object");
    }
    auto key = keyFromValue(keyValue);
    if (!key) {
      throw std::runtime_error("OBJECT_HAS expects string/number/bool key");
    }
    auto *obj = heap_.object(std::get<ObjectRef>(object).id);
    if (!obj) {
      push(BytecodeValue(false));
    } else {
      push(BytecodeValue(obj->find(*key) != obj->end()));
    }
    break;
  }

  case OpCode::OBJECT_DELETE: {
    BytecodeValue keyValue = pop();
    BytecodeValue object = pop();
    if (!std::holds_alternative<ObjectRef>(object)) {
      throw std::runtime_error("OBJECT_DELETE expects object");
    }
    auto key = keyFromValue(keyValue);
    if (!key) {
      throw std::runtime_error("OBJECT_DELETE expects string/number/bool key");
    }
    auto *obj = heap_.object(std::get<ObjectRef>(object).id);
    if (!obj) {
      push(BytecodeValue(false));
    } else {
      push(BytecodeValue(obj->erase(*key) > 0));
    }
    break;
  }

  // Array intrinsics (VM-level operations)
  case OpCode::ARRAY_POP: {
    BytecodeValue array = pop();
    if (!std::holds_alternative<ArrayRef>(array)) {
      throw std::runtime_error("ARRAY_POP expects array");
    }
    auto *arr = heap_.array(std::get<ArrayRef>(array).id);
    if (!arr || arr->empty()) {
      push(BytecodeValue(nullptr));
    } else {
      push(arr->back());
      arr->pop_back();
    }
    break;
  }

  case OpCode::ARRAY_HAS: {
    BytecodeValue value = pop();
    BytecodeValue array = pop();
    if (!std::holds_alternative<ArrayRef>(array)) {
      throw std::runtime_error("ARRAY_HAS expects array");
    }
    auto *arr = heap_.array(std::get<ArrayRef>(array).id);
    if (!arr) {
      push(BytecodeValue(false));
    } else {
      bool found = false;
      for (const auto& elem : *arr) {
        if (valuesEqual(elem, value)) {
          found = true;
          break;
        }
      }
      push(BytecodeValue(found));
    }
    break;
  }

  case OpCode::ARRAY_FIND: {
    BytecodeValue value = pop();
    BytecodeValue array = pop();
    if (!std::holds_alternative<ArrayRef>(array)) {
      throw std::runtime_error("ARRAY_FIND expects array");
    }
    auto *arr = heap_.array(std::get<ArrayRef>(array).id);
    if (!arr) {
      push(BytecodeValue(int64_t(-1)));
    } else {
      int64_t foundIdx = -1;
      for (size_t i = 0; i < arr->size(); i++) {
        if (valuesEqual((*arr)[i], value)) {
          foundIdx = static_cast<int64_t>(i);
          break;
        }
      }
      push(BytecodeValue(foundIdx));
    }
    break;
  }

  // Array higher-order functions (VM intrinsics)
  case OpCode::ARRAY_MAP: {
    BytecodeValue fn = pop();
    BytecodeValue array = pop();
    if (!std::holds_alternative<ArrayRef>(array)) {
      throw std::runtime_error("ARRAY_MAP expects array");
    }
    auto *arr = heap_.array(std::get<ArrayRef>(array).id);
    if (!arr) {
      push(BytecodeValue(nullptr));
      break;
    }
    if (!std::holds_alternative<FunctionObject>(fn) && !std::holds_alternative<ClosureRef>(fn)) {
      throw std::runtime_error("ARRAY_MAP expects function/closure");
    }

    auto resultRef = heap_.allocateArray();
    auto *result = heap_.array(resultRef.id);
    uint64_t resultRootId = pinExternalRoot(BytecodeValue(resultRef));

    for (size_t i = 0; i < arr->size(); i++) {
      BytecodeValue mapped = callFunctionSync(fn, {(*arr)[i]});
      result->push_back(mapped);
    }

    unpinExternalRoot(resultRootId);
    push(BytecodeValue(resultRef));
    break;
  }

  case OpCode::ARRAY_FILTER: {
    BytecodeValue fn = pop();
    BytecodeValue array = pop();
    if (!std::holds_alternative<ArrayRef>(array)) {
      throw std::runtime_error("ARRAY_FILTER expects array");
    }
    auto *arr = heap_.array(std::get<ArrayRef>(array).id);
    if (!arr) {
      push(BytecodeValue(nullptr));
      break;
    }

    auto resultRef = heap_.allocateArray();
    auto *result = heap_.array(resultRef.id);
    uint64_t resultRootId = pinExternalRoot(BytecodeValue(resultRef));

    for (size_t i = 0; i < arr->size(); i++) {
      BytecodeValue predResult = callFunctionSync(fn, {(*arr)[i]});
      if (std::holds_alternative<bool>(predResult) && std::get<bool>(predResult)) {
        result->push_back((*arr)[i]);
      }
    }

    unpinExternalRoot(resultRootId);
    push(BytecodeValue(resultRef));
    break;
  }

  case OpCode::ARRAY_REDUCE: {
    BytecodeValue initial = pop();
    BytecodeValue fn = pop();
    BytecodeValue array = pop();
    if (!std::holds_alternative<ArrayRef>(array)) {
      throw std::runtime_error("ARRAY_REDUCE expects array");
    }
    auto *arr = heap_.array(std::get<ArrayRef>(array).id);
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
    if (!std::holds_alternative<ArrayRef>(array)) {
      throw std::runtime_error("ARRAY_FOREACH expects array");
    }
    auto *arr = heap_.array(std::get<ArrayRef>(array).id);
    if (!arr) {
      push(BytecodeValue(nullptr));
      break;
    }

    for (size_t i = 0; i < arr->size(); i++) {
      (void)callFunctionSync(fn, {(*arr)[i]});
    }

    push(BytecodeValue(nullptr));
    break;
  }

  // String intrinsics (VM-level operations)
  case OpCode::STRING_LEN: {
    BytecodeValue str = pop();
    if (!std::holds_alternative<std::string>(str)) {
      throw std::runtime_error("STRING_LEN expects string");
    }
    push(BytecodeValue(int64_t(std::get<std::string>(str).length())));
    break;
  }

  case OpCode::STRING_UPPER: {
    BytecodeValue str = pop();
    if (!std::holds_alternative<std::string>(str)) {
      throw std::runtime_error("STRING_UPPER expects string");
    }
    std::string s = std::get<std::string>(str);
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    push(BytecodeValue(s));
    break;
  }

  case OpCode::STRING_LOWER: {
    BytecodeValue str = pop();
    if (!std::holds_alternative<std::string>(str)) {
      throw std::runtime_error("STRING_LOWER expects string");
    }
    std::string s = std::get<std::string>(str);
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    push(BytecodeValue(s));
    break;
  }

  case OpCode::STRING_TRIM: {
    BytecodeValue str = pop();
    if (!std::holds_alternative<std::string>(str)) {
      throw std::runtime_error("STRING_TRIM expects string");
    }
    std::string s = std::get<std::string>(str);
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) {
      push(BytecodeValue(std::string("")));
    } else {
      size_t end = s.find_last_not_of(" \t\n\r");
      push(BytecodeValue(s.substr(start, end - start + 1)));
    }
    break;
  }

  case OpCode::STRING_HAS: {
    BytecodeValue substr = pop();
    BytecodeValue str = pop();
    if (!std::holds_alternative<std::string>(str) || !std::holds_alternative<std::string>(substr)) {
      throw std::runtime_error("STRING_HAS expects strings");
    }
    const std::string& s = std::get<std::string>(str);
    const std::string& sub = std::get<std::string>(substr);
    push(BytecodeValue(s.find(sub) != std::string::npos));
    break;
  }

  case OpCode::STRING_STARTS: {
    BytecodeValue prefix = pop();
    BytecodeValue str = pop();
    if (!std::holds_alternative<std::string>(str) || !std::holds_alternative<std::string>(prefix)) {
      throw std::runtime_error("STRING_STARTS expects strings");
    }
    const std::string& s = std::get<std::string>(str);
    const std::string& pre = std::get<std::string>(prefix);
    push(BytecodeValue(s.size() >= pre.size() && s.compare(0, pre.size(), pre) == 0));
    break;
  }

  case OpCode::STRING_ENDS: {
    BytecodeValue suffix = pop();
    BytecodeValue str = pop();
    if (!std::holds_alternative<std::string>(str) || !std::holds_alternative<std::string>(suffix)) {
      throw std::runtime_error("STRING_ENDS expects strings");
    }
    const std::string& s = std::get<std::string>(str);
    const std::string& suf = std::get<std::string>(suffix);
    push(BytecodeValue(s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0));
    break;
  }

  // Spread operator - spread array elements
  case OpCode::SPREAD: {
    BytecodeValue value = pop();
    if (std::holds_alternative<ArrayRef>(value)) {
      auto arrRef = std::get<ArrayRef>(value);
      auto *arr = heap_.array(arrRef.id);
      if (arr) {
        // Push each element individually
        for (auto &elem : *arr) {
          push(elem);
        }
      }
    } else if (std::holds_alternative<std::string>(value)) {
      // Spread string into characters
      auto str = std::get<std::string>(value);
      auto arrRef = heap_.allocateArray();
      auto *arr = heap_.array(arrRef.id);
      if (arr) {
        for (char c : str) {
          arr->push_back(BytecodeValue(std::string(1, c)));
        }
        push(BytecodeValue(arrRef));
      }
    }
    break;
  }

  // Spread in function call
  case OpCode::SPREAD_CALL: {
    // Similar to SPREAD but marks arguments for spread in CALL
    BytecodeValue value = pop();
    if (std::holds_alternative<ArrayRef>(value)) {
      auto arrRef = std::get<ArrayRef>(value);
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
        std::get<std::string>(instruction.operands[0]);
    BytecodeValue value = pop();

    if (typeName == "int" || typeName == "Int") {
      push(toInt(value));
    } else if (typeName == "float" || typeName == "Float" ||
               typeName == "double" || typeName == "num" || typeName == "Num") {
      push(toFloat(value));
    } else if (typeName == "string" || typeName == "String") {
      push(BytecodeValue(toString(value)));
    } else if (typeName == "bool" || typeName == "Bool" ||
               typeName == "boolean") {
      push(toBool(value));
    } else if (typeName == "array" || typeName == "Array") {
      // Convert to array if possible
      if (std::holds_alternative<ArrayRef>(value)) {
        push(value);
      } else {
        auto arrRef = heap_.allocateArray();
        push(BytecodeValue(arrRef));
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
    push(BytecodeValue(toString(value)));
    break;
  }

  // String concatenation
  case OpCode::STRING_CONCAT: {
    BytecodeValue right = pop();
    BytecodeValue left = pop();
    std::string leftStr = toString(left);
    std::string rightStr = toString(right);
    push(BytecodeValue(leftStr + rightStr));
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
    if (std::holds_alternative<std::nullptr_t>(value)) {
      typeName = "null";
    } else if (std::holds_alternative<bool>(value)) {
      typeName = "bool";
    } else if (std::holds_alternative<int64_t>(value)) {
      typeName = "int";
    } else if (std::holds_alternative<double>(value)) {
      typeName = "num";  // Renamed from "float" to "num" per docs
    } else if (std::holds_alternative<std::string>(value)) {
      typeName = "string";
    } else if (std::holds_alternative<ArrayRef>(value)) {
      typeName = "array";
    } else if (std::holds_alternative<ObjectRef>(value)) {
      typeName = "object";
    } else if (std::holds_alternative<HostFunctionRef>(value)) {
      typeName = "function";
    } else {
      typeName = "unknown";
    }
    push(BytecodeValue(typeName));
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
  if (!std::holds_alternative<ClosureRef>(closure) && 
      !std::holds_alternative<FunctionObject>(closure)) {
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
  return std::holds_alternative<std::nullptr_t>(value);
}

bool VM::isTruthy(const BytecodeValue &value) {
  // Step 1: null is always falsy
  if (isNull(value)) {
    return false;
  }

  // Step 2: boolean values follow their own truthiness
  if (std::holds_alternative<bool>(value)) {
    return std::get<bool>(value);
  }

  // Step 3: numeric values: 0 is falsy, non-zero is truthy
  if (std::holds_alternative<int64_t>(value)) {
    return std::get<int64_t>(value) != 0;
  }
  if (std::holds_alternative<double>(value)) {
    return std::get<double>(value) != 0.0;
  }

  // Step 4: empty string is falsy, non-empty is truthy
  if (std::holds_alternative<std::string>(value)) {
    return !std::get<std::string>(value).empty();
  }

  // Step 5: arrays are truthy if non-empty
  if (std::holds_alternative<ArrayRef>(value)) {
    auto *array = heap_.array(std::get<ArrayRef>(value).id);
    return array && !array->empty();
  }

  // Step 6: objects are always truthy (even if empty)
  if (std::holds_alternative<ObjectRef>(value)) {
    return true;
  }

  // Step 7: sets are truthy if non-empty
  if (std::holds_alternative<SetRef>(value)) {
    // Note: Sets aren't fully implemented yet, but assume truthy if they exist
    return true;
  }

  // Step 8: functions are always truthy
  if (std::holds_alternative<FunctionObject>(value) ||
      std::holds_alternative<HostFunctionRef>(value) ||
      std::holds_alternative<ClosureRef>(value)) {
    return true;
  }

  // Step 9: constant pool indices should never appear here, but treat as falsy
  if (std::holds_alternative<uint32_t>(value)) {
    return false;
  }

  // Default: should not reach here, but be conservative
  return false;
}

// Duration parsing utility
std::optional<int64_t> VM::parseDuration(const BytecodeValue &value) const {
  if (std::holds_alternative<int64_t>(value)) {
    return std::get<int64_t>(value);
  }

  if (std::holds_alternative<double>(value)) {
    return static_cast<int64_t>(std::get<double>(value));
  }

  if (std::holds_alternative<std::string>(value)) {
    const std::string &duration_str = std::get<std::string>(value);

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

} // namespace havel::compiler
