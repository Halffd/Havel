#pragma once

#include "BytecodeIR.hpp"
#include <functional>
#include <vector>
#include <string>
#include <unordered_map>
#include <type_traits>
#include <memory>

namespace havel::compiler {

// ============================================================================
// NativeFunctionBridge - C++ function binding to VM
// ============================================================================
class NativeFunctionBridge {
public:
  using NativeFunction = std::function<BytecodeValue(const std::vector<BytecodeValue>&)>;
  using NativeFunctionVariadic = std::function<BytecodeValue(std::span<const BytecodeValue>)>;

  // Function traits for automatic type conversion
  template<typename T>
  struct TypeTraits {
    static T fromValue(const BytecodeValue& value);
    static BytecodeValue toValue(const T& value);
    static const char* name();
  };

  // Register a native function
  template<typename ReturnType, typename... ArgTypes>
  void registerFunction(const std::string& name,
                        ReturnType (*func)(ArgTypes...));

  // Register with std::function
  void registerFunction(const std::string& name, NativeFunction func);
  void registerFunction(const std::string& name, NativeFunctionVariadic func, int arity = -1);

  // Call a native function
  BytecodeValue call(const std::string& name, const std::vector<BytecodeValue>& args) const;

  // Check if function exists
  bool hasFunction(const std::string& name) const;

  // Get function info
  struct FunctionInfo {
    std::string name;
    int arity;
    bool isVariadic;
    std::string returnTypeName;
    std::vector<std::string> paramTypeNames;
  };

  std::optional<FunctionInfo> getFunctionInfo(const std::string& name) const;
  std::vector<std::string> listFunctions() const;

  // Remove function
  void unregisterFunction(const std::string& name);
  void clear();

  // Arity checking
  bool checkArity(const std::string& name, size_t argCount) const;
  std::optional<int> getArity(const std::string& name) const;

private:
  struct RegisteredFunction {
    NativeFunction func;
    FunctionInfo info;
  };

  std::unordered_map<std::string, RegisteredFunction> functions_;

  // Template helpers for automatic binding
  template<typename ReturnType, typename... ArgTypes, size_t... Indices>
  BytecodeValue invokeWithArgs(ReturnType (*func)(ArgTypes...),
                               const std::vector<BytecodeValue>& args,
                               std::index_sequence<Indices...>) const;
};

// ============================================================================
// RuntimeTypeSystem - Runtime type checking and operations
// ============================================================================
class RuntimeTypeSystem {
public:
  enum class Type : uint8_t {
    Null,
    Boolean,
    Integer,
    Float,
    String,
    Array,
    Object,
    Function,
    Closure,
    Struct,
    Class,
    Enum,
    Iterator,
    NativeData,
    Type // Meta-type
  };

  struct TypeInfo {
    Type type;
    std::string name;
    size_t size;
    bool isReference;
    bool isCallable;
    bool isIterable;
    std::vector<Type> baseTypes;
  };

  RuntimeTypeSystem();

  // Type registration
  void registerType(Type type, const TypeInfo& info);
  std::optional<TypeInfo> getTypeInfo(Type type) const;

  // Type checking
  static Type getType(const BytecodeValue& value);
  static std::string typeName(const BytecodeValue& value);
  static bool isTruthy(const BytecodeValue& value);
  static bool isNull(const BytecodeValue& value);
  static bool isNumber(const BytecodeValue& value);
  static bool isInteger(const BytecodeValue& value);
  static bool isString(const BytecodeValue& value);
  static bool isArray(const BytecodeValue& value);
  static bool isObject(const BytecodeValue& value);
  static bool isCallable(const BytecodeValue& value);

  // Type coercion
  static std::optional<int64_t> toInteger(const BytecodeValue& value);
  static std::optional<double> toFloat(const BytecodeValue& value);
  static std::optional<std::string> toString(const BytecodeValue& value);
  static std::optional<bool> toBoolean(const BytecodeValue& value);

  // Type conversion
  static BytecodeValue convert(const BytecodeValue& value, Type targetType);

  // Type checking with error
  static bool checkType(const BytecodeValue& value, Type expected,
                       std::string& errorMessage);

  // Generic operations
  static bool equals(const BytecodeValue& a, const BytecodeValue& b);
  static int compare(const BytecodeValue& a, const BytecodeValue& b);
  static std::string stringify(const BytecodeValue& value);
  static size_t hash(const BytecodeValue& value);

  // Type hierarchy
  bool isSubtype(Type subtype, Type supertype) const;
  bool canConvert(Type from, Type to) const;

private:
  std::unordered_map<Type, TypeInfo> typeRegistry_;
  std::unordered_map<std::string, Type> nameToType_;

  void registerBuiltinTypes();
};

// ============================================================================
// IteratorProtocol - Standard iterator interface
// ============================================================================
class IteratorProtocol {
public:
  virtual ~IteratorProtocol() = default;

  // Iterator operations
  virtual bool hasNext() const = 0;
  virtual BytecodeValue next() = 0;
  virtual BytecodeValue peek() const = 0;

  // Reset if possible
  virtual bool canReset() const { return false; }
  virtual void reset() {}

  // Get underlying value
  virtual BytecodeValue getIterable() const = 0;
};

// ============================================================================
// IterableFactory - Creates iterators from values
// ============================================================================
class IterableFactory {
public:
  using IteratorCreator = std::function<std::unique_ptr<IteratorProtocol>(const BytecodeValue&)>;

  void registerIterator(Type type, IteratorCreator creator);

  std::unique_ptr<IteratorProtocol> createIterator(const BytecodeValue& value);
  bool isIterable(const BytecodeValue& value) const;

  // Standard iterators
  static std::unique_ptr<IteratorProtocol> createArrayIterator(const BytecodeValue& array);
  static std::unique_ptr<IteratorProtocol> createObjectIterator(const BytecodeValue& object);
  static std::unique_ptr<IteratorProtocol> createRangeIterator(const BytecodeValue& range);
  static std::unique_ptr<IteratorProtocol> createStringIterator(const BytecodeValue& string);

private:
  std::unordered_map<Type, IteratorCreator> creators_;
};

// ============================================================================
// ExceptionHandler - Structured exception handling
// ============================================================================
class ExceptionHandler {
public:
  struct CatchClause {
    std::optional<Type> typeFilter; // Null means catch all
    std::string variableName;
    uint32_t handlerAddress;
  };

  struct TryBlock {
    uint32_t startAddress;
    uint32_t endAddress;
    std::vector<CatchClause> catchClauses;
    std::optional<uint32_t> finallyAddress;
    uint32_t savedStackDepth;
  };

  void pushTryBlock(const TryBlock& block);
  void popTryBlock();

  // Exception handling
  std::optional<uint32_t> findHandler(const BytecodeValue& exception,
                                       uint32_t currentAddress) const;
  bool hasFinallyBlock() const;
  uint32_t getFinallyAddress() const;

  // Stack unwinding
  void unwindTo(uint32_t targetDepth);
  uint32_t currentStackDepth() const { return tryBlocks_.size(); }

  // Rethrow
  void setPendingException(const BytecodeValue& ex);
  bool hasPendingException() const;
  BytecodeValue getPendingException() const;
  void clearPendingException();

private:
  std::vector<TryBlock> tryBlocks_;
  std::optional<BytecodeValue> pendingException_;
};

// ============================================================================
// ValueSerializer - Serialize/deserialize values
// ============================================================================
class ValueSerializer {
public:
  enum class Format {
    Binary,
    JSON,
    MessagePack
  };

  // Serialization
  std::string serialize(const BytecodeValue& value, Format format = Format::Binary);
  std::vector<uint8_t> serializeBinary(const BytecodeValue& value);
  std::string serializeJSON(const BytecodeValue& value);

  // Deserialization
  std::optional<BytecodeValue> deserialize(const std::string& data,
                                              Format format = Format::Binary);
  std::optional<BytecodeValue> deserializeBinary(std::span<const uint8_t> data);
  std::optional<BytecodeValue> deserializeJSON(const std::string& json);

  // Chunk serialization
  std::vector<uint8_t> serializeChunk(const BytecodeChunk& chunk);
  std::optional<BytecodeChunk> deserializeChunk(std::span<const uint8_t> data);

private:
  std::string valueToJson(const BytecodeValue& value);
  BytecodeValue jsonToValue(const std::string& json);
};

} // namespace havel::compiler
