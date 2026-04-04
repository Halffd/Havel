#pragma once

#include "../../core/Value.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

// Forward declarations
namespace havel::ast {
struct Program;
}

namespace havel::compiler {

using Value = havel::core::Value;

// Forward declarations
class BytecodeCompiler;
class BytecodeInterpreter;
class JITCompiler;
class ByteCompiler;

// Bytecode instruction format
enum class OpCode : uint8_t {
  // Stack operations
  LOAD_CONST,
  LOAD_GLOBAL,
  STORE_GLOBAL, // Store to global variable by name
  LOAD_VAR,
  STORE_VAR,
  LOAD_UPVALUE,
  STORE_UPVALUE,
  POP,
  DUP,
  SWAP,
  PUSH_NULL,

  // Arithmetic operations
  ADD,
  SUB,
  MUL,
  DIV,
  MOD,
  POW,

  // Comparison operations
  EQ,
  NEQ,
  LT,
  LTE,
  GT,
  GTE,

  // Logical operations
  AND,
  OR,
  NOT,
  NEGATE,  // Unary minus (negate number)
  IS_NULL, // Check if value is null or undefined

  // Control flow
  JUMP,
  JUMP_IF_FALSE,
  JUMP_IF_TRUE,
  JUMP_IF_NULL, // Jump only if null or undefined (for ?? operator)
  CALL,
  TAIL_CALL, // Tail call optimization - reuse current frame
  CALL_HOST,
  RETURN,
  TRY_ENTER,      // Install exception handler (catch ip)
  TRY_EXIT,       // Remove active exception handler
  LOAD_EXCEPTION, // Push current exception object
  THROW,          // Throw exception object

  // Function operations
  DEFINE_FUNC,
  CLOSURE,

  // Array operations
  ARRAY_NEW,
  ARRAY_GET,
  ARRAY_SET,
  ARRAY_PUSH,
  ARRAY_LEN,

  // Range operations
  RANGE_NEW,      // Create range: start..end or start..step..end
  RANGE_STEP_NEW, // Create range with step

  // Struct operations (compact storage, field access by index)
  STRUCT_NEW, // Create struct with field count
  STRUCT_GET, // Get field by index
  STRUCT_SET, // Set field by index

  // Enum operations (tagged union)
  ENUM_NEW,     // Create enum variant (tag + payload count)
  ENUM_TAG,     // Get enum tag
  ENUM_PAYLOAD, // Get payload by index
  ENUM_MATCH,   // Pattern match on enum

  // Object operations (VM intrinsics)
  OBJECT_KEYS,    // Get array of keys
  OBJECT_VALUES,  // Get array of values
  OBJECT_ENTRIES, // Get array of [key, value] pairs
  OBJECT_HAS,     // Check if key exists
  OBJECT_DELETE,  // Delete key from object

  // Array operations (additional VM intrinsics)
  ARRAY_POP,     // Pop element from array
  ARRAY_HAS,     // Check if array has value
  ARRAY_FIND,    // Find index of value
  ARRAY_MAP,     // Map function over array
  ARRAY_FILTER,  // Filter array by predicate
  ARRAY_REDUCE,  // Reduce array to single value
  ARRAY_FOREACH, // Execute function for each element

  // String operations (VM intrinsics)
  STRING_LEN,     // Get string length
  STRING_UPPER,   // Convert to uppercase
  STRING_LOWER,   // Convert to lowercase
  STRING_TRIM,    // Trim whitespace
  STRING_SUB,     // Substring
  STRING_FIND,    // Find substring
  STRING_HAS,     // Check if contains substring
  STRING_STARTS,  // Check if starts with
  STRING_ENDS,    // Check if ends with
  STRING_SPLIT,   // Split by delimiter
  STRING_REPLACE, // Replace substring

  // Iteration protocol
  ITER_NEW,  // Create iterator from iterable
  ITER_NEXT, // Get next {value, done} from iterator
  SET_NEW,

  // Object operations
  OBJECT_NEW,
  OBJECT_NEW_UNSORTED, // Create object with unsorted keys (!{} syntax)
  OBJECT_GET,
  OBJECT_SET,

  // String operations
  STRING_CONCAT,

  // Spread operator
  SPREAD,
  SPREAD_CALL,

  // Type conversion
  AS_TYPE,
  TO_INT,
  TO_FLOAT,
  TO_STRING,
  TO_BOOL,
  TYPE_OF,

  // Special operations
  PRINT,
  DEBUG,
  // Class operations (with prototype chain support)
  CLASS_NEW,       // Create class with parent: typeId, parentTypeId, fieldCount
  CLASS_GET_FIELD, // Get field with prototype chain lookup
  CLASS_SET_FIELD, // Set field with prototype chain lookup
  LOAD_CLASS_PROTO, // Load parent class reference
  CALL_SUPER,       // Call method from parent class
  NOP
};

struct FunctionObject {
  uint32_t function_index = 0;
};

struct ClosureRef {
  uint32_t id = 0;
};

struct ArrayRef {
  uint32_t id = 0;
};

struct ObjectRef {
  uint32_t id = 0;
  bool sorted = true; // Default to sorted keys
};

struct SetRef {
  uint32_t id = 0;
};

struct RangeRef {
  uint32_t id = 0;
};

// Struct: compact field storage (fields stored as array, type info separate)
struct StructRef {
  uint32_t id = 0;     // GC object id for the field array
  uint32_t typeId = 0; // Type registry index
};

// Class: reference type with methods (shared identity)
struct ClassRef {
  uint32_t id = 0;       // GC object id for the field array
  uint32_t typeId = 0;   // Type registry index
  uint32_t parentId = 0; // Parent class instance id (0 for none)
};

// Enum: tagged union (tag + payload array)
struct EnumRef {
  uint32_t id = 0;     // GC object id for the payload array
  uint32_t tag = 0;    // Variant tag
  uint32_t typeId = 0; // Type registry index
};

struct IteratorRef {
  uint32_t id = 0;
};

struct HostFunctionRef {
  std::string name;
};

struct LazyPipelineRef {
  uint32_t id = 0; // GC object id storing the lazy chain
};

// Error: custom error type with stack trace and metadata
struct ErrorRef {
  uint32_t id = 0; // GC object id for the error object
};

using BytecodeHostFunction =
    std::function<Value(const std::vector<Value> &)>;

struct SourceLocation {
  std::string filename;
  uint32_t line = 0;
  uint32_t column = 0;
  uint32_t length = 0;
};

// Bytecode instruction
struct Instruction {
  OpCode opcode;
  std::vector<Value> operands;
  std::optional<SourceLocation> location;

  Instruction() : opcode(OpCode::NOP) {}
  Instruction(OpCode op, std::vector<Value> ops = {})
      : opcode(op), operands(std::move(ops)) {}
};

// Reserved for Phase 2 closure capture support.
struct UpvalueDescriptor {
  uint32_t index = 0;
  bool captures_local = false;
};

// Bytecode function
struct BytecodeFunction {
  std::string name;
  std::vector<Instruction> instructions;
  std::vector<SourceLocation> instruction_locations;
  std::vector<Value> constants;
  std::vector<UpvalueDescriptor> upvalues;
  uint32_t param_count;
  uint32_t local_count;
  // Default parameter values (indexed by param index, empty if no default)
  // Stored as constant values for simple defaults
  std::vector<std::optional<Value>> default_values;
  // Index of variadic parameter (UINT32_MAX if none)
  uint32_t variadic_param_index = UINT32_MAX;

  BytecodeFunction(std::string n, uint32_t params = 0, uint32_t locals = 0)
      : name(std::move(n)), param_count(params), local_count(locals) {}
};

// Bytecode chunk (compiled module)
class BytecodeChunk {
private:
  std::vector<BytecodeFunction> functions;
  std::unordered_map<std::string, uint32_t> function_indices;

public:
  void addFunction(BytecodeFunction func) {
    uint32_t index = functions.size();
    function_indices[func.name] = index;
    functions.push_back(std::move(func));
  }

  const BytecodeFunction *getFunction(const std::string &name) const {
    auto it = function_indices.find(name);
    return it != function_indices.end() ? &functions[it->second] : nullptr;
  }

  const BytecodeFunction *getFunction(uint32_t index) const {
    if (index >= functions.size()) {
      return nullptr;
    }
    return &functions[index];
  }

  const std::vector<BytecodeFunction> &getAllFunctions() const {
    return functions;
  }

  size_t getFunctionCount() const { return functions.size(); }

  uint32_t addString(std::string str) {
    for (uint32_t i = 0; i < strings.size(); i++) {
      if (strings[i] == str) return i;
    }
    strings.push_back(std::move(str));
    return static_cast<uint32_t>(strings.size() - 1);
  }

  const std::string& getString(uint32_t index) const {
    static const std::string empty;
    if (index >= strings.size()) return empty;
    return strings[index];
  }

  const std::vector<std::string>& getAllStrings() const { return strings; }

private:
  std::vector<std::string> strings;
};

// Bytecode compiler interface
class BytecodeCompiler {
public:
  virtual ~BytecodeCompiler() = default;
  virtual std::unique_ptr<BytecodeChunk>
  compile(const ast::Program &program) = 0;
};

// Bytecode interpreter interface
class BytecodeInterpreter {
public:
  virtual ~BytecodeInterpreter() = default;
  virtual Value execute(const BytecodeChunk &chunk, const std::string &function_name,
                        const std::vector<Value> &args = {}) = 0;
  virtual void setDebugMode(bool enabled) = 0;
  virtual void registerHostFunction(const std::string &name,
                                    BytecodeHostFunction function) {
    (void)name;
    (void)function;
  }
  virtual bool hasHostFunction(const std::string &name) const {
    (void)name;
    return false;
  }
};

// JIT compiler interface
class JITCompiler {
public:
  virtual ~JITCompiler() = default;
  virtual void compileFunction(const BytecodeFunction &func) = 0;
  virtual Value executeCompiled(const std::string &func_name,
                                const std::vector<Value> &args) = 0;
  virtual bool isCompiled(const std::string &func_name) const = 0;
};

// Hybrid execution engine (Compiler + Interpreter + JIT)
class Hybrid {
protected:
  std::unique_ptr<BytecodeCompiler> compiler;
  std::unique_ptr<BytecodeInterpreter> interpreter;
  std::unique_ptr<JITCompiler> jit;
  std::unique_ptr<BytecodeChunk> current_chunk;

  // Performance tracking for JIT decisions
  std::unordered_map<std::string, uint32_t> execution_counts;
  bool jit_enabled = true;
  uint32_t jit_threshold = 100; // Compile after 100 executions

public:
  Hybrid(std::unique_ptr<BytecodeCompiler> comp,
         std::unique_ptr<BytecodeInterpreter> interp,
         std::unique_ptr<JITCompiler> jcomp = nullptr);

  virtual ~Hybrid() = default;

  virtual bool compile(const ast::Program &program);
  virtual Value execute(const std::string &function_name,
                        const std::vector<Value> &args = {});

  // Configure JIT
  void setJITEnabled(bool enabled) { jit_enabled = enabled; }
  void setJITThreshold(uint32_t threshold) { jit_threshold = threshold; }

  // Performance stats
  std::unordered_map<std::string, uint32_t> getExecutionStats() const {
    return execution_counts;
  }
  void resetStats() { execution_counts.clear(); }
};

std::unique_ptr<BytecodeInterpreter> createVM();
std::unique_ptr<Hybrid> createHybrid();

} // namespace havel::compiler
