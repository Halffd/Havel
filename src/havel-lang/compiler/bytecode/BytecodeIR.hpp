#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

// Forward declarations
namespace havel::ast {
struct Program;
}

namespace havel::compiler {

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
  LOAD_VAR,
  STORE_VAR,
  LOAD_UPVALUE,
  STORE_UPVALUE,
  POP,
  DUP,
  SWAP,

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

  // Control flow
  JUMP,
  JUMP_IF_FALSE,
  JUMP_IF_TRUE,
  CALL,
  CALL_HOST,
  RETURN,

  // Function operations
  DEFINE_FUNC,
  CLOSURE,

  // Array operations
  ARRAY_NEW,
  ARRAY_GET,
  ARRAY_SET,
  ARRAY_PUSH,
  SET_NEW,

  // Object operations
  OBJECT_NEW,
  OBJECT_GET,
  OBJECT_SET,

  // Special operations
  PRINT,
  DEBUG,
  NOP
};

// Bytecode value type
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
};

struct SetRef {
  uint32_t id = 0;
};

struct HostFunctionRef {
  std::string name;
};

using BytecodeValue = std::variant<
    std::nullptr_t, bool, int64_t, double, std::string,
    uint32_t, // Index into constant pool
    FunctionObject, ClosureRef, ArrayRef, ObjectRef, SetRef, HostFunctionRef>;

using BytecodeHostFunction =
    std::function<BytecodeValue(const std::vector<BytecodeValue> &)>;

// Bytecode instruction
struct Instruction {
  OpCode opcode;
  std::vector<BytecodeValue> operands;

  Instruction(OpCode op, std::vector<BytecodeValue> ops = {})
      : opcode(op), operands(std::move(ops)) {}
};

// Reserved for Phase 2 closure capture support.
struct UpvalueDescriptor {
  uint32_t index = 0;
  bool captures_local = false;
};

struct SourceLocation {
  uint32_t line = 0;
  uint32_t column = 0;
};

// Bytecode function
struct BytecodeFunction {
  std::string name;
  std::vector<Instruction> instructions;
  std::vector<SourceLocation> instruction_locations;
  std::vector<BytecodeValue> constants;
  std::vector<UpvalueDescriptor> upvalues;
  uint32_t param_count;
  uint32_t local_count;

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
  virtual BytecodeValue
  execute(const BytecodeChunk &chunk, const std::string &function_name,
          const std::vector<BytecodeValue> &args = {}) = 0;
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
  virtual BytecodeValue
  executeCompiled(const std::string &func_name,
                  const std::vector<BytecodeValue> &args) = 0;
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
  virtual BytecodeValue execute(const std::string &function_name,
                                const std::vector<BytecodeValue> &args = {});

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
