#include "RuntimeSupport.hpp"
#include <sstream>
#include <iomanip>

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
  NativeFunction wrapper = [func](const std::vector<BytecodeValue>& args) -> BytecodeValue {
    return func(std::span<const BytecodeValue>(args));
  };

  RegisteredFunction reg;
  reg.func = wrapper;
  reg.info.name = name;
  reg.info.arity = arity;
  reg.info.isVariadic = (arity < 0);
  functions_[name] = reg;
}

BytecodeValue NativeFunctionBridge::call(const std::string& name,
                                         const std::vector<BytecodeValue>& args) const {
  auto it = functions_.find(name);
  if (it == functions_.end()) {
    throw std::runtime_error("Native function not found: " + name);
  }

  const auto& reg = it->second;

  // Check arity if known
  if (reg.info.arity >= 0 && static_cast<int>(args.size()) != reg.info.arity) {
    throw std::runtime_error("Arity mismatch for " + name);
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

RuntimeTypeSystem::Type RuntimeTypeSystem::getType(const BytecodeValue& value) {
  return std::visit([](const auto& val) -> Type {
    using T = std::decay_t<decltype(val)>;
    if constexpr (std::is_same_v<T, nullptr_t>) {
      return Type::Null;
    } else if constexpr (std::is_same_v<T, bool>) {
      return Type::Boolean;
    } else if constexpr (std::is_same_v<T, int64_t>) {
      return Type::Integer;
    } else if constexpr (std::is_same_v<T, double>) {
      return Type::Float;
    } else if constexpr (std::is_same_v<T, std::string>) {
      return Type::String;
    } else if constexpr (std::is_same_v<T, ArrayRef>) {
      return Type::Array;
    } else if constexpr (std::is_same_v<T, ObjectRef>) {
      return Type::Object;
    } else if constexpr (std::is_same_v<T, FunctionObject>) {
      return Type::Function;
    } else if constexpr (std::is_same_v<T, ClosureRef>) {
      return Type::Closure;
    } else if constexpr (std::is_same_v<T, StructRef>) {
      return Type::Struct;
    } else if constexpr (std::is_same_v<T, ClassRef>) {
      return Type::Class;
    } else if constexpr (std::is_same_v<T, EnumRef>) {
      return Type::Enum;
    } else if constexpr (std::is_same_v<T, IteratorRef>) {
      return Type::Iterator;
    } else if constexpr (std::is_same_v<T, NativeDataRef>) {
      return Type::NativeData;
    } else {
      return Type::Null;
    }
  }, value);
}

std::string RuntimeTypeSystem::typeName(const BytecodeValue& value) {
  auto info = getTypeInfo(getType(value));
  if (info) return info->name;
  return "unknown";
}

bool RuntimeTypeSystem::isTruthy(const BytecodeValue& value) {
  return std::visit([](const auto& val) -> bool {
    using T = std::decay_t<decltype(val)>;
    if constexpr (std::is_same_v<T, nullptr_t>) {
      return false;
    } else if constexpr (std::is_same_v<T, bool>) {
      return val;
    } else if constexpr (std::is_same_v<T, int64_t>) {
      return val != 0;
    } else if constexpr (std::is_same_v<T, double>) {
      return val != 0.0;
    } else if constexpr (std::is_same_v<T, std::string>) {
      return !val.empty();
    } else {
      return true; // All reference types are truthy
    }
  }, value);
}

bool RuntimeTypeSystem::isNull(const BytecodeValue& value) {
  return std::holds_alternative<nullptr_t>(value);
}

bool RuntimeTypeSystem::isNumber(const BytecodeValue& value) {
  return std::holds_alternative<int64_t>(value) || std::holds_alternative<double>(value);
}

bool RuntimeTypeSystem::isInteger(const BytecodeValue& value) {
  return std::holds_alternative<int64_t>(value);
}

bool RuntimeTypeSystem::isString(const BytecodeValue& value) {
  return std::holds_alternative<std::string>(value);
}

bool RuntimeTypeSystem::isArray(const BytecodeValue& value) {
  return std::holds_alternative<ArrayRef>(value);
}

bool RuntimeTypeSystem::isObject(const BytecodeValue& value) {
  return std::holds_alternative<ObjectRef>(value);
}

bool RuntimeTypeSystem::isCallable(const BytecodeValue& value) {
  Type type = getType(value);
  auto info = getTypeInfo(type);
  return info && info->isCallable;
}

std::optional<int64_t> RuntimeTypeSystem::toInteger(const BytecodeValue& value) {
  return std::visit([](const auto& val) -> std::optional<int64_t> {
    using T = std::decay_t<decltype(val)>;
    if constexpr (std::is_same_v<T, int64_t>) {
      return val;
    } else if constexpr (std::is_same_v<T, double>) {
      return static_cast<int64_t>(val);
    } else if constexpr (std::is_same_v<T, bool>) {
      return val ? 1 : 0;
    } else if constexpr (std::is_same_v<T, std::string>) {
      try {
        return std::stoll(val);
      } catch (...) {
        return std::nullopt;
      }
    } else {
      return std::nullopt;
    }
  }, value);
}

std::optional<double> RuntimeTypeSystem::toFloat(const BytecodeValue& value) {
  return std::visit([](const auto& val) -> std::optional<double> {
    using T = std::decay_t<decltype(val)>;
    if constexpr (std::is_same_v<T, double>) {
      return val;
    } else if constexpr (std::is_same_v<T, int64_t>) {
      return static_cast<double>(val);
    } else if constexpr (std::is_same_v<T, std::string>) {
      try {
        return std::stod(val);
      } catch (...) {
        return std::nullopt;
      }
    } else {
      return std::nullopt;
    }
  }, value);
}

std::optional<std::string> RuntimeTypeSystem::toString(const BytecodeValue& value) {
  return std::visit([](const auto& val) -> std::optional<std::string> {
    using T = std::decay_t<decltype(val)>;
    if constexpr (std::is_same_v<T, std::string>) {
      return val;
    } else if constexpr (std::is_same_v<T, int64_t>) {
      return std::to_string(val);
    } else if constexpr (std::is_same_v<T, double>) {
      return std::to_string(val);
    } else if constexpr (std::is_same_v<T, bool>) {
      return val ? "true" : "false";
    } else if constexpr (std::is_same_v<T, nullptr_t>) {
      return "null";
    } else {
      return "<object>"; // Simplified
    }
  }, value);
}

std::optional<bool> RuntimeTypeSystem::toBoolean(const BytecodeValue& value) {
  return isTruthy(value);
}

BytecodeValue RuntimeTypeSystem::convert(const BytecodeValue& value, Type targetType) {
  Type sourceType = getType(value);
  if (sourceType == targetType) return value;

  switch (targetType) {
    case Type::Integer: {
      auto result = toInteger(value);
      return result ? *result : BytecodeValue(nullptr);
    }
    case Type::Float: {
      auto result = toFloat(value);
      return result ? *result : BytecodeValue(nullptr);
    }
    case Type::String: {
      auto result = toString(value);
      return result ? *result : BytecodeValue(nullptr);
    }
    case Type::Boolean:
      return toBoolean(value);
    default:
      return BytecodeValue(nullptr);
  }
}

bool RuntimeTypeSystem::checkType(const BytecodeValue& value, Type expected,
                                  std::string& errorMessage) {
  Type actual = getType(value);
  if (actual == expected || isSubtype(actual, expected)) {
    return true;
  }

  auto actualInfo = getTypeInfo(actual);
  auto expectedInfo = getTypeInfo(expected);

  errorMessage = "Type mismatch: expected " +
                 (expectedInfo ? expectedInfo->name : "unknown") +
                 ", got " +
                 (actualInfo ? actualInfo->name : "unknown");
  return false;
}

bool RuntimeTypeSystem::equals(const BytecodeValue& a, const BytecodeValue& b) {
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

  return std::visit([&b](const auto& val) -> bool {
    using T = std::decay_t<decltype(val)>;
    if constexpr (std::is_same_v<T, int64_t> || std::is_same_v<T, double> ||
                  std::is_same_v<T, bool> || std::is_same_v<T, std::string> ||
                  std::is_same_v<T, nullptr_t>) {
      return val == std::get<T>(b);
    } else {
      // For reference types, compare IDs
      return val == std::get<T>(b);
    }
  }, a);
}

int RuntimeTypeSystem::compare(const BytecodeValue& a, const BytecodeValue& b) {
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

std::string RuntimeTypeSystem::stringify(const BytecodeValue& value) {
  auto result = toString(value);
  return result ? *result : "<unknown>";
}

size_t RuntimeTypeSystem::hash(const BytecodeValue& value) {
  return std::visit([](const auto& val) -> size_t {
    using T = std::decay_t<decltype(val)>;
    if constexpr (std::is_same_v<T, int64_t>) {
      return std::hash<int64_t>{}(val);
    } else if constexpr (std::is_same_v<T, double>) {
      return std::hash<double>{}(val);
    } else if constexpr (std::is_same_v<T, bool>) {
      return std::hash<bool>{}(val);
    } else if constexpr (std::is_same_v<T, std::string>) {
      return std::hash<std::string>{}(val);
    } else {
      return 0;
    }
  }, value);
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

void IterableFactory::registerIterator(Type type, IteratorCreator creator) {
  creators_[type] = creator;
}

std::unique_ptr<IteratorProtocol> IterableFactory::createIterator(const BytecodeValue& value) {
  Type type = RuntimeTypeSystem::getType(value);

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

bool IterableFactory::isIterable(const BytecodeValue& value) const {
  Type type = RuntimeTypeSystem::getType(value);

  if (creators_.count(type) > 0) return true;

  auto info = RuntimeTypeSystem().getTypeInfo(type);
  return info && info->isIterable;
}

std::unique_ptr<IteratorProtocol> IterableFactory::createArrayIterator(const BytecodeValue& array) {
  (void)array;
  // TODO: Implement array iterator
  return nullptr;
}

std::unique_ptr<IteratorProtocol> IterableFactory::createObjectIterator(const BytecodeValue& object) {
  (void)object;
  // TODO: Implement object iterator
  return nullptr;
}

std::unique_ptr<IteratorProtocol> IterableFactory::createRangeIterator(const BytecodeValue& range) {
  (void)range;
  // TODO: Implement range iterator
  return nullptr;
}

std::unique_ptr<IteratorProtocol> IterableFactory::createStringIterator(const BytecodeValue& string) {
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

std::optional<uint32_t> ExceptionHandler::findHandler(const BytecodeValue& exception,
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

void ExceptionHandler::setPendingException(const BytecodeValue& ex) {
  pendingException_ = ex;
}

bool ExceptionHandler::hasPendingException() const {
  return pendingException_.has_value();
}

BytecodeValue ExceptionHandler::getPendingException() const {
  return pendingException_.value_or(nullptr);
}

void ExceptionHandler::clearPendingException() {
  pendingException_.reset();
}

// ============================================================================
// ValueSerializer Implementation
// ============================================================================

std::string ValueSerializer::serialize(const BytecodeValue& value, Format format) {
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

std::vector<uint8_t> ValueSerializer::serializeBinary(const BytecodeValue& value) {
  std::vector<uint8_t> result;

  // Simple binary format: type tag + data
  std::visit([&result](const auto& val) {
    using T = std::decay_t<decltype(val)>;
    if constexpr (std::is_same_v<T, nullptr_t>) {
      result.push_back(0);
    } else if constexpr (std::is_same_v<T, bool>) {
      result.push_back(1);
      result.push_back(val ? 1 : 0);
    } else if constexpr (std::is_same_v<T, int64_t>) {
      result.push_back(2);
      for (int i = 0; i < 8; ++i) {
        result.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
      }
    } else if constexpr (std::is_same_v<T, double>) {
      result.push_back(3);
      // Simplified - just write bytes
      const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&val);
      result.insert(result.end(), bytes, bytes + sizeof(double));
    } else if constexpr (std::is_same_v<T, std::string>) {
      result.push_back(4);
      uint32_t len = val.size();
      for (int i = 0; i < 4; ++i) {
        result.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
      }
      result.insert(result.end(), val.begin(), val.end());
    }
  }, value);

  return result;
}

std::string ValueSerializer::serializeJSON(const BytecodeValue& value) {
  return valueToJson(value);
}

std::optional<BytecodeValue> ValueSerializer::deserialize(const std::string& data,
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

std::optional<BytecodeValue> ValueSerializer::deserializeBinary(std::span<const uint8_t> data) {
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
        return str;
      }
    default:
      return std::nullopt;
  }
}

std::optional<BytecodeValue> ValueSerializer::deserializeJSON(const std::string& json) {
  return jsonToValue(json);
}

std::vector<uint8_t> ValueSerializer::serializeChunk(const BytecodeChunk& chunk) {
  // Simplified chunk serialization
  (void)chunk;
  return {};
}

std::optional<BytecodeChunk> ValueSerializer::deserializeChunk(std::span<const uint8_t> data) {
  (void)data;
  return std::nullopt;
}

std::string ValueSerializer::valueToJson(const BytecodeValue& value) {
  return std::visit([](const auto& val) -> std::string {
    using T = std::decay_t<decltype(val)>;
    if constexpr (std::is_same_v<T, nullptr_t>) {
      return "null";
    } else if constexpr (std::is_same_v<T, bool>) {
      return val ? "true" : "false";
    } else if constexpr (std::is_same_v<T, int64_t>) {
      return std::to_string(val);
    } else if constexpr (std::is_same_v<T, double>) {
      return std::to_string(val);
    } else if constexpr (std::is_same_v<T, std::string>) {
      return "\"" + val + "\"";
    } else {
      return "null";
    }
  }, value);
}

BytecodeValue ValueSerializer::jsonToValue(const std::string& json) {
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
    return json.substr(1, json.size() - 2);
  }

  return nullptr;
}

} // namespace havel::compiler
