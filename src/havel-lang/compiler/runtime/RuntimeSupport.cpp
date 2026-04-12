#include "havel-lang/errors/ErrorSystem.h"
#include "RuntimeSupport.hpp"
#include <sstream>
#include <iomanip>
#include <cstring>
#include <stdexcept>

// Macro for throwing errors with source location info
#define COMPILER_THROW(msg) \
  do { \
    ::havel::errors::ErrorReporter::instance().report( \
        HAVEL_ERROR(::havel::errors::ErrorStage::Compiler, msg)); \
    throw std::runtime_error(std::string(msg) + " [" __FILE__ ":" + std::to_string(__LINE__) + "]"); \
  } while (0)

namespace havel::compiler {

// ============================================================================
// NativeFunctionBridge Implementation
// ============================================================================

void NativeFunctionBridge::registerFunction(const std::string& name, NativeFunction func) {
  RegisteredFunction reg;
  reg.func = func;
  reg.info.name = name;
  reg.info.arity = -1; // Unknown
  reg.info.isVariadic = false;
  functions_[name] = reg;
}

void NativeFunctionBridge::registerFunction(const std::string& name,
                                            NativeFunctionVariadic func,
                                            int arity) {
  // Wrap variadic function
  NativeFunction wrapper = [func](const std::vector<Value>& args) -> Value {
    return func(std::span<const Value>(args));
  };

  RegisteredFunction reg;
  reg.func = wrapper;
  reg.info.name = name;
  reg.info.arity = arity;
  reg.info.isVariadic = (arity < 0);
  functions_[name] = reg;
}

Value NativeFunctionBridge::call(const std::string& name,
                                         const std::vector<Value>& args) const {
  auto it = functions_.find(name);
  if (it == functions_.end()) {
    COMPILER_THROW("Native function not found: " + name);
  }

  const auto& reg = it->second;

  // Check arity if known
  if (reg.info.arity >= 0 && static_cast<int>(args.size()) != reg.info.arity) {
    COMPILER_THROW("Arity mismatch for " + name);
  }

  return reg.func(args);
}

bool NativeFunctionBridge::hasFunction(const std::string& name) const {
  return functions_.count(name) > 0;
}

std::optional<NativeFunctionBridge::FunctionInfo> NativeFunctionBridge::getFunctionInfo(
    const std::string& name) const {
  auto it = functions_.find(name);
  if (it != functions_.end()) {
    return it->second.info;
  }
  return std::nullopt;
}

std::vector<std::string> NativeFunctionBridge::listFunctions() const {
  std::vector<std::string> result;
  for (const auto& [name, _] : functions_) {
    (void)_;
    result.push_back(name);
  }
  return result;
}

void NativeFunctionBridge::unregisterFunction(const std::string& name) {
  functions_.erase(name);
}

void NativeFunctionBridge::clear() {
  functions_.clear();
}

bool NativeFunctionBridge::checkArity(const std::string& name, size_t argCount) const {
  auto info = getFunctionInfo(name);
  if (!info) return false;
  if (info->isVariadic) return true;
  return static_cast<int>(argCount) == info->arity;
}

std::optional<int> NativeFunctionBridge::getArity(const std::string& name) const {
  auto info = getFunctionInfo(name);
  if (info) return info->arity;
  return std::nullopt;
}

// ============================================================================
// RuntimeTypeSystem Implementation
// ============================================================================

RuntimeTypeSystem::RuntimeTypeSystem() {
  registerBuiltinTypes();
}

void RuntimeTypeSystem::registerBuiltinTypes() {
  auto makeInfo = [](Type type, const char* name, size_t size, bool ref, bool callable, bool iterable) {
    TypeInfo info;
    info.type = type;
    info.name = name;
    info.size = size;
    info.isReference = ref;
    info.isCallable = callable;
    info.isIterable = iterable;
    return info;
  };

  registerType(Type::Null, makeInfo(Type::Null, "null", 0, false, false, false));
  registerType(Type::Boolean, makeInfo(Type::Boolean, "boolean", 1, false, false, false));
  registerType(Type::Integer, makeInfo(Type::Integer, "integer", 8, false, false, false));
  registerType(Type::Float, makeInfo(Type::Float, "float", 8, false, false, false));
  registerType(Type::String, makeInfo(Type::String, "string", 0, true, false, true));
  registerType(Type::Array, makeInfo(Type::Array, "array", 0, true, false, true));
  registerType(Type::Object, makeInfo(Type::Object, "object", 0, true, false, true));
  registerType(Type::Function, makeInfo(Type::Function, "function", 0, true, true, false));
  registerType(Type::Closure, makeInfo(Type::Closure, "closure", 0, true, true, false));
  registerType(Type::Struct, makeInfo(Type::Struct, "struct", 0, false, false, false));
  registerType(Type::Class, makeInfo(Type::Class, "class", 0, true, true, false));
  registerType(Type::Enum, makeInfo(Type::Enum, "enum", 0, false, false, false));
  registerType(Type::Iterator, makeInfo(Type::Iterator, "iterator", 0, true, false, true));
  registerType(Type::NativeData, makeInfo(Type::NativeData, "native", 0, true, false, false));
}

void RuntimeTypeSystem::registerType(Type type, const TypeInfo& info) {
  typeRegistry_[type] = info;
  nameToType_[info.name] = type;
}

std::optional<RuntimeTypeSystem::TypeInfo> RuntimeTypeSystem::getTypeInfo(Type type) const {
  auto it = typeRegistry_.find(type);
  if (it != typeRegistry_.end()) {
    return it->second;
  }
  return std::nullopt;
}

RuntimeTypeSystem::Type RuntimeTypeSystem::getType(const Value& value) {
  if (value.isNull()) return Type::Null;
  if (value.isBool()) return Type::Boolean;
  if (value.isInt()) return Type::Integer;
  if (value.isDouble()) return Type::Float;
  if (value.isStringValId() || value.isStringId()) return Type::String;
  if (value.isArrayId()) return Type::Array;
  if (value.isObjectId()) return Type::Object;
  if (value.isFunctionObjId()) return Type::Function;
  if (value.isClosureId()) return Type::Closure;

  if (value.isEnumId()) return Type::Enum;
  if (value.isIteratorId()) return Type::Iterator;
  return Type::Null;
}

std::string RuntimeTypeSystem::typeName(const Value& value) {
  RuntimeTypeSystem rts;
  auto info = rts.getTypeInfo(getType(value));
  if (info) return info->name;
  return "unknown";
}

bool RuntimeTypeSystem::isTruthy(const Value& value) {
  if (value.isNull()) return false;
  if (value.isBool()) return value.asBool();
  if (value.isInt()) return value.asInt() != 0;
  if (value.isDouble()) return value.asDouble() != 0.0;
  if (value.isStringValId()) return true; // All strings are truthy
  return true; // All reference types are truthy
}

bool RuntimeTypeSystem::isNull(const Value& value) {
  return value.isNull();
}

bool RuntimeTypeSystem::isNumber(const Value& value) {
  return value.isInt() || value.isDouble();
}

bool RuntimeTypeSystem::isInteger(const Value& value) {
  return value.isInt();
}

bool RuntimeTypeSystem::isString(const Value& value) {
  return value.isStringValId();
}

bool RuntimeTypeSystem::isArray(const Value& value) {
  return value.isArrayId();
}

bool RuntimeTypeSystem::isObject(const Value& value) {
  return value.isObjectId();
}

bool RuntimeTypeSystem::isCallable(const Value& value) {
  Type type = getType(value);
  RuntimeTypeSystem rts;
  auto info = rts.getTypeInfo(type);
  return info && info->isCallable;
}

std::optional<int64_t> RuntimeTypeSystem::toInteger(const Value& value) {
  if (value.isInt()) return value.asInt();
  if (value.isDouble()) return static_cast<int64_t>(value.asDouble());
  if (value.isBool()) return value.asBool() ? 1 : 0;
  // TODO: string pool lookup for string conversion
  return std::nullopt;
}

std::optional<double> RuntimeTypeSystem::toFloat(const Value& value) {
  if (value.isDouble()) return value.asDouble();
  if (value.isInt()) return static_cast<double>(value.asInt());
  // TODO: string pool lookup for string conversion
  return std::nullopt;
}

std::optional<std::string> RuntimeTypeSystem::toString(const Value& value) {
  if (value.isInt()) return std::to_string(value.asInt());
  if (value.isDouble()) return std::to_string(value.asDouble());
  if (value.isBool()) return value.asBool() ? "true" : "false";
  if (value.isNull()) return "null";
  // TODO: string pool lookup for string conversion
  return "<object>"; // Simplified
}

std::optional<bool> RuntimeTypeSystem::toBoolean(const Value& value) {
  return isTruthy(value);
}

Value RuntimeTypeSystem::convert(const Value& value, Type targetType) {
  Type sourceType = getType(value);
  if (sourceType == targetType) return value;

  switch (targetType) {
    case Type::Integer: {
      auto result = toInteger(value);
      return result ? *result : Value(nullptr);
    }
    case Type::Float: {
      auto result = toFloat(value);
      return result ? Value::makeDouble(*result) : Value::makeNull();
    }
    case Type::String: {
      auto result = toString(value);
      // TODO: string pool integration
      return result ? Value::makeNull() : Value::makeNull();
    }
    case Type::Boolean:
      return Value::makeBool(toBoolean(value).value_or(false));
    default:
      return Value::makeNull();
  }
}

bool RuntimeTypeSystem::checkType(const Value& value, Type expected,
                                  std::string& errorMessage) {
  Type actual = getType(value);
  RuntimeTypeSystem rts;
  if (actual == expected || rts.isSubtype(actual, expected)) {
    return true;
  }

  auto actualInfo = rts.getTypeInfo(actual);
  auto expectedInfo = rts.getTypeInfo(expected);

  errorMessage = "Type mismatch: expected " +
                 (expectedInfo ? expectedInfo->name : "unknown") +
                 ", got " +
                 (actualInfo ? actualInfo->name : "unknown");
  return false;
}

bool RuntimeTypeSystem::equals(const Value& a, const Value& b) {
  if (getType(a) != getType(b)) {
    // Try numeric comparison
    if (isNumber(a) && isNumber(b)) {
      auto ai = toInteger(a);
      auto bi = toInteger(b);
      if (ai && bi) return *ai == *bi;

      auto ad = toFloat(a);
      auto bd = toFloat(b);
      if (ad && bd) return *ad == *bd;
    }
    return false;
  }

  // Same type - compare values directly
  if (a.isNull()) return true; // null == null
  if (a.isBool()) return a.asBool() == b.asBool();
  if (a.isInt()) return a.asInt() == b.asInt();
  if (a.isDouble()) return a.asDouble() == b.asDouble();
  if (a.isStringValId()) return a.asStringValId() == b.asStringValId();
  if (a.isFunctionObjId()) return a.asFunctionObjId() == b.asFunctionObjId();
  if (a.isHostFuncId()) return a.asHostFuncId() == b.asHostFuncId();
  if (a.isArrayId()) return a.asArrayId() == b.asArrayId();
  if (a.isObjectId()) return a.asObjectId() == b.asObjectId();
  if (a.isRangeId()) return a.asRangeId() == b.asRangeId();
  if (a.isIteratorId()) return a.asIteratorId() == b.asIteratorId();

  if (a.isEnumId()) return a.asEnumId() == b.asEnumId();
  if (a.isClosureId()) return a.asClosureId() == b.asClosureId();
  return false;
}

int RuntimeTypeSystem::compare(const Value& a, const Value& b) {
  if (!isNumber(a) || !isNumber(b)) {
    return 0; // Can't compare
  }

  auto ad = toFloat(a);
  auto bd = toFloat(b);
  if (!ad || !bd) return 0;

  if (*ad < *bd) return -1;
  if (*ad > *bd) return 1;
  return 0;
}

std::string RuntimeTypeSystem::stringify(const Value& value) {
  auto result = toString(value);
  return result ? *result : "<unknown>";
}

size_t RuntimeTypeSystem::hash(const Value& value) {
  if (value.isInt()) return std::hash<int64_t>{}(value.asInt());
  if (value.isDouble()) return std::hash<double>{}(value.asDouble());
  if (value.isBool()) return std::hash<bool>{}(value.asBool());
  if (value.isStringValId()) return std::hash<uint32_t>{}(value.asStringValId());
  if (value.isNull()) return 0;
  // For reference types, hash the ID
  if (value.isArrayId()) return std::hash<uint32_t>{}(value.asArrayId());
  if (value.isObjectId()) return std::hash<uint32_t>{}(value.asObjectId());
  if (value.isFunctionObjId()) return std::hash<uint32_t>{}(value.asFunctionObjId());
  if (value.isHostFuncId()) return std::hash<uint32_t>{}(value.asHostFuncId());
  return 0;
}

bool RuntimeTypeSystem::isSubtype(Type subtype, Type supertype) const {
  if (subtype == supertype) return true;

  auto info = getTypeInfo(subtype);
  if (!info) return false;

  for (Type base : info->baseTypes) {
    if (isSubtype(base, supertype)) return true;
  }

  return false;
}

bool RuntimeTypeSystem::canConvert(Type from, Type to) const {
  if (from == to || isSubtype(from, to)) return true;

  // Numeric conversions
  if ((from == Type::Integer || from == Type::Float) &&
      (to == Type::Integer || to == Type::Float)) {
    return true;
  }

  // To string
  if (to == Type::String) return true;

  // To boolean
  if (to == Type::Boolean) return true;

  return false;
}

// ============================================================================
// IterableFactory Implementation
// ============================================================================

void IterableFactory::registerIterator(RuntimeTypeSystem::Type type, IteratorCreator creator) {
  creators_[type] = creator;
}

std::unique_ptr<IteratorProtocol> IterableFactory::createIterator(const Value& value) {
  RuntimeTypeSystem::Type type = RuntimeTypeSystem::getType(value);

  auto it = creators_.find(type);
  if (it != creators_.end()) {
    return it->second(value);
  }

  // Fallback to standard iterators
  if (RuntimeTypeSystem::isArray(value)) {
    return createArrayIterator(value);
  } else if (RuntimeTypeSystem::isObject(value)) {
    return createObjectIterator(value);
  } else if (RuntimeTypeSystem::isString(value)) {
    return createStringIterator(value);
  }

  return nullptr;
}

bool IterableFactory::isIterable(const Value& value) const {
  RuntimeTypeSystem::Type type = RuntimeTypeSystem::getType(value);

  if (creators_.count(type) > 0) return true;

  auto info = RuntimeTypeSystem().getTypeInfo(type);
  return info && info->isIterable;
}

std::unique_ptr<IteratorProtocol> IterableFactory::createArrayIterator(const Value& array) {
  (void)array;
  // TODO: Implement array iterator
  return nullptr;
}

std::unique_ptr<IteratorProtocol> IterableFactory::createObjectIterator(const Value& object) {
  (void)object;
  // TODO: Implement object iterator
  return nullptr;
}

std::unique_ptr<IteratorProtocol> IterableFactory::createRangeIterator(const Value& range) {
  (void)range;
  // TODO: Implement range iterator
  return nullptr;
}

std::unique_ptr<IteratorProtocol> IterableFactory::createStringIterator(const Value& string) {
  (void)string;
  // TODO: Implement string iterator
  return nullptr;
}

// ============================================================================
// ExceptionHandler Implementation
// ============================================================================

void ExceptionHandler::pushTryBlock(const TryBlock& block) {
  tryBlocks_.push_back(block);
}

void ExceptionHandler::popTryBlock() {
  if (!tryBlocks_.empty()) {
    tryBlocks_.pop_back();
  }
}

std::optional<uint32_t> ExceptionHandler::findHandler(const Value& exception,
                                                       uint32_t currentAddress) const {
  (void)exception;

  // Search from innermost to outermost
  for (auto it = tryBlocks_.rbegin(); it != tryBlocks_.rend(); ++it) {
    if (currentAddress >= it->startAddress && currentAddress < it->endAddress) {
      // Find matching catch clause
      for (const auto& clause : it->catchClauses) {
        (void)clause;
        // Type checking would go here
        return clause.handlerAddress;
      }
    }
  }

  return std::nullopt;
}

bool ExceptionHandler::hasFinallyBlock() const {
  if (tryBlocks_.empty()) return false;
  return tryBlocks_.back().finallyAddress.has_value();
}

uint32_t ExceptionHandler::getFinallyAddress() const {
  if (tryBlocks_.empty()) return 0;
  return tryBlocks_.back().finallyAddress.value_or(0);
}

void ExceptionHandler::unwindTo(uint32_t targetDepth) {
  while (tryBlocks_.size() > targetDepth) {
    tryBlocks_.pop_back();
  }
}

void ExceptionHandler::setPendingException(const Value& ex) {
  pendingException_ = ex;
}

bool ExceptionHandler::hasPendingException() const {
  return pendingException_.has_value();
}

Value ExceptionHandler::getPendingException() const {
  return pendingException_.value_or(nullptr);
}

void ExceptionHandler::clearPendingException() {
  pendingException_.reset();
}

// ============================================================================
// ValueSerializer Implementation
// ============================================================================

std::string ValueSerializer::serialize(const Value& value, Format format) {
  switch (format) {
    case Format::Binary:
      return std::string(serializeBinary(value).begin(), serializeBinary(value).end());
    case Format::JSON:
      return serializeJSON(value);
    case Format::MessagePack:
      // Not implemented
      return "";
  }
  return "";
}

std::vector<uint8_t> ValueSerializer::serializeBinary(const Value& value) {
  std::vector<uint8_t> result;

  // Simple binary format: type tag + data
  if (value.isNull()) {
    result.push_back(0);
  } else if (value.isBool()) {
    result.push_back(1);
    result.push_back(value.asBool() ? 1 : 0);
  } else if (value.isInt()) {
    result.push_back(2);
    int64_t val = value.asInt();
    for (int i = 0; i < 8; ++i) {
      result.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
    }
  } else if (value.isDouble()) {
    result.push_back(3);
    // Simplified - just write bytes
    double val = value.asDouble();
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&val);
    result.insert(result.end(), bytes, bytes + sizeof(double));
  } else if (value.isStringValId()) {
    result.push_back(4);
    // TODO: string pool integration - write string ID
    uint32_t len = 0;
    for (int i = 0; i < 4; ++i) {
      result.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
    }
  }

  return result;
}

std::string ValueSerializer::serializeJSON(const Value& value) {
  return valueToJson(value);
}

std::optional<Value> ValueSerializer::deserialize(const std::string& data,
                                                           Format format) {
  switch (format) {
    case Format::Binary:
      return deserializeBinary(std::vector<uint8_t>(data.begin(), data.end()));
    case Format::JSON:
      return deserializeJSON(data);
    case Format::MessagePack:
      return std::nullopt;
  }
  return std::nullopt;
}

std::optional<Value> ValueSerializer::deserializeBinary(std::span<const uint8_t> data) {
  if (data.empty()) return std::nullopt;

  uint8_t type = data[0];

  switch (type) {
    case 0:
      return nullptr;
    case 1:
      if (data.size() < 2) return std::nullopt;
      return data[1] != 0;
    case 2:
      if (data.size() < 9) return std::nullopt;
      {
        int64_t val = 0;
        for (int i = 0; i < 8; ++i) {
          val |= static_cast<int64_t>(data[1 + i]) << (i * 8);
        }
        return val;
      }
    case 3:
      if (data.size() < 9) return std::nullopt;
      {
        double val;
        memcpy(&val, &data[1], sizeof(double));
        return val;
      }
    case 4:
      if (data.size() < 5) return std::nullopt;
      {
        uint32_t len = 0;
        for (int i = 0; i < 4; ++i) {
          len |= static_cast<uint32_t>(data[1 + i]) << (i * 8);
        }
        if (data.size() < 5 + len) return std::nullopt;
        std::string str(data.begin() + 5, data.begin() + 5 + len);
        // TODO: string pool integration
        return Value::makeNull();
      }
    default:
      return std::nullopt;
  }
}

std::optional<Value> ValueSerializer::deserializeJSON(const std::string& json) {
  return jsonToValue(json);
}

std::vector<uint8_t> ValueSerializer::serializeChunk(const BytecodeChunk& chunk) {
  std::vector<uint8_t> data;
  auto append = [&data](const void* ptr, size_t size) {
    if (ptr == nullptr || size == 0) return;
    data.insert(data.end(), static_cast<const uint8_t*>(ptr),
                static_cast<const uint8_t*>(ptr) + size);
  };

  // Header: "HVC1" magic
  append("HVC1", 4);

  const auto& functions = chunk.getAllFunctions();
  const auto& strings = chunk.getAllStrings();

  // Number of functions (uint32_t)
  uint32_t numFuncs = static_cast<uint32_t>(functions.size());
  append(&numFuncs, sizeof(numFuncs));

  // Number of strings (uint32_t)
  uint32_t numStrings = static_cast<uint32_t>(strings.size());
  append(&numStrings, sizeof(numStrings));

  // Serialize string table
  for (const auto& s : strings) {
    uint32_t len = static_cast<uint32_t>(s.size());
    append(&len, sizeof(len));
    if (!s.empty()) append(s.data(), s.size());
  }

  // Serialize functions
  for (const auto& func : functions) {
    uint32_t nameLen = static_cast<uint32_t>(func.name.size());
    append(&nameLen, sizeof(nameLen));
    if (!func.name.empty()) append(func.name.data(), func.name.size());

    append(&func.param_count, sizeof(func.param_count));
    append(&func.local_count, sizeof(func.local_count));

    uint32_t numInstr = static_cast<uint32_t>(func.instructions.size());
    append(&numInstr, sizeof(numInstr));
    for (const auto& instr : func.instructions) {
      uint8_t opcode = static_cast<uint8_t>(instr.opcode);
      append(&opcode, sizeof(opcode));
      uint32_t numOps = static_cast<uint32_t>(instr.operands.size());
      append(&numOps, sizeof(numOps));
      for (const auto& op : instr.operands) {
        uint64_t opVal = op.rawBits();
        append(&opVal, sizeof(opVal));
      }

      uint32_t numConsts = static_cast<uint32_t>(func.constants.size());
      append(&numConsts, sizeof(numConsts));
      for (const auto& c : func.constants) {
        uint64_t raw = c.rawBits();
        append(&raw, sizeof(raw));
      }

      uint32_t numUp = static_cast<uint32_t>(func.upvalues.size());
      append(&numUp, sizeof(numUp));
      for (const auto& u : func.upvalues) {
        append(&u.index, sizeof(u.index));
        append(&u.captures_local, sizeof(u.captures_local));
      }
    }
  }

  return data;
}

std::optional<BytecodeChunk> ValueSerializer::deserializeChunk(std::span<const uint8_t> data) {
  (void)data;
  return std::nullopt;
}

std::string ValueSerializer::valueToJson(const Value& value) {
  if (value.isNull()) return "null";
  if (value.isBool()) return value.asBool() ? "true" : "false";
  if (value.isInt()) return std::to_string(value.asInt());
  if (value.isDouble()) return std::to_string(value.asDouble());
  if (value.isStringValId()) {
    // TODO: string pool integration
    return "\"<string>\"";
  }
  return "null";
}

Value ValueSerializer::jsonToValue(const std::string& json) {
  // Simplified JSON parsing
  if (json == "null") return nullptr;
  if (json == "true") return true;
  if (json == "false") return false;

  // Try integer
  try {
    size_t pos;
    int64_t val = std::stoll(json, &pos);
    if (pos == json.size()) return val;
  } catch (...) {}

  // Try double
  try {
    size_t pos;
    double val = std::stod(json, &pos);
    if (pos == json.size()) return val;
  } catch (...) {}

  // String (remove quotes)
  if (json.size() >= 2 && json.front() == '"' && json.back() == '"') {
    // TODO: string pool integration
    return Value::makeNull();
  }

  return nullptr;
}

} // namespace havel::compiler
