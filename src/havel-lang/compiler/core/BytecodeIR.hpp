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

namespace havel::compiler {

// Type feedback for JIT specialization
// Type tags for AOT type hints (must match Value::Tag)
constexpr uint64_t TYPE_HINT_INT = 1ULL << 0;      // int48
constexpr uint64_t TYPE_HINT_NUMBER = 1ULL << 1;  // f64 (double)
constexpr uint64_t TYPE_HINT_STRING = 1ULL << 2;  // string
constexpr uint64_t TYPE_HINT_ARRAY = 1ULL << 3;   // array
constexpr uint64_t TYPE_HINT_OBJECT = 1ULL << 4;  // object
constexpr uint64_t TYPE_HINT_BOOL = 1ULL << 5;    // boolean
constexpr uint64_t TYPE_HINT_NULL = 1ULL << 6;    // null
constexpr uint64_t TYPE_HINT_FUNCTION = 1ULL << 7; // function/closure

struct TypeFeedback {
    uint64_t left_type_mask = 0;
    uint64_t right_type_mask = 0;
    uint64_t result_type_mask = 0;
    uint32_t execution_count = 0;

    // AOT type hints from annotations (set at compile time)
    uint64_t aot_type_hint = 0;  // Type hint for result (e.g., TYPE_HINT_INT for "x: int")
    bool has_aot_hint = false;   // Whether we have a compile-time type hint
};


} // namespace havel::compiler

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
    STORE_IMMUT_GLOBAL, // Store to global and mark immutable (val)
    LOAD_VAR,
    STORE_VAR,
    STORE_IMMUT_VAR, // Store to local and mark immutable (val)
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
  INT_DIV,
  DIVMOD,
  REMAINDER,
  MOD,
  POW,

  // Compound assignment operations
  ADD_ASSIGN,
  SUB_ASSIGN,
  MUL_ASSIGN,
  DIV_ASSIGN,
  INT_DIV_ASSIGN,
  REMAINDER_ASSIGN,
  MOD_ASSIGN,
  POW_ASSIGN,
  BITWISE_AND_ASSIGN,
  BITWISE_OR_ASSIGN,
  BITWISE_XOR_ASSIGN,
  SHIFT_LEFT_ASSIGN,
  SHIFT_RIGHT_ASSIGN,

// Increment/Decrement local variable (optimization)
INCLOCAL,       // ++local (prefix increment)
DECLOCAL,       // --local (prefix decrement)
INCLOCAL_POST,  // local++ (postfix increment)
DECLOCAL_POST,  // local-- (postfix decrement)

// Comparison operations
  EQ,
  NEQ,
  IS,     // Identity comparison (same object reference)
  LT,
  LTE,
  GT,
  GTE,

    // Logical operations
    AND,
    OR,
    NOT,
    NEGATE, // Unary minus (negate number)
    IS_NULL, // Check if value is null or undefined

    // Bitwise operations
    BIT_AND,
    BIT_OR,
    BIT_XOR,
    BIT_LSH,
    BIT_RSH,
	BIT_NOT, // Unary bitwise NOT (~)
	LENGTH, // # operator - dispatch to op_length or any.len

	// Control flow
  JUMP,
  JUMP_IF_FALSE,
  JUMP_IF_TRUE,
  JUMP_IF_NULL, // Jump only if null or undefined (for ?? operator)
CALL,
  CALL_DYN,  // Dynamic arg count from stack (for spread args)
  CALL_SPREAD, // Spread call: combine literal args + spread array + call
  TAIL_CALL, // Tail call optimization - reuse current frame
  CALL_METHOD, // Dispatch method call based on value type (no boxing)
  CALL_METHOD_SPREAD, // Method call with dynamic spread: combine args + lookup + call
  FFI_CALL, // Direct libffi call (fn_ptr, ret_type, param_types, args...)
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
  ARRAY_DEL,    // Delete array element at index
  ARRAY_PUSH,
  ARRAY_LEN,
  ARRAY_FREEZE, // Mark top-of-stack array as frozen (for tuples)

  // Fast array access (with inline caching)
  ARRAY_GET_FAST, // Fast array get with inline caching (array_id, ip in operands)
  ARRAY_SET_FAST, // Fast array set with inline caching

  // Fast integer arithmetic (unboxed)
  ADD_INT,
  SUB_INT,
  MUL_INT,
  DIV_INT,
  MOD_INT,

  // Set operations
  SET_SET, // Set[key] = value (stack: value, key, set)
  SET_DEL, // Set.del(key) or del set[key] (stack: key, set)

  // Range operations
  RANGE_NEW,      // Create range: start..end or start..step..end
  RANGE_STEP_NEW, // Create range with step


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
  OBJECT_GET_RAW, // Get property without method binding (for super calls)

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
  STRING_PROMOTE, // Convert StringValId → StringId (for runtime string iteration)

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
  STRING_GET_FAST,   // Fast string get with inline caching (string_id, ip in operands)
  STRING_SET_FAST,   // Fast string set with inline caching
  STRING_GET_FAST_IP, // Fast string get with inline caching + IP in operands
  STRING_SET_FAST_IP, // Fast string set with inline caching + IP in operands
  // String cursor (native UTF-8 cursor with position state)
  STRING_CURSOR_NEW,     // Create cursor from string: string_id -> cursor
  STRING_CURSOR_CURRENT, // Get current codepoint: cursor -> int/char
  STRING_CURSOR_ADVANCE, // Advance cursor: cursor -> bool (false if at end)
  STRING_CURSOR_PEEK,    // Peek at next codepoint without advancing: cursor -> int/char
  STRING_CURSOR_RESET,   // Reset cursor to start: cursor -> void
  STRING_CURSOR_GET_POS, // Get byte position: cursor -> int
  STRING_CURSOR_SET_POS, // Set byte position: cursor, int -> void

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
    IMPORT, // Runtime module import (path -> module object)
    IMPORT_WILDCARD, // Import all exports from module object as globals

  // Struct intrinsics (bypass generic CALL path)
  STRUCT_NEW,       // Create struct instance: typeNameId + arg_count
  STRUCT_GET,       // Get struct field by name id
  STRUCT_SET,       // Set struct field by name id

  // Protocol intrinsics
  PROT_CHECK,       // Runtime protocol check by name id (value -> bool)
  PROT_CAST,        // Runtime checked cast by protocol name id (value -> value/null)
  
  // Concurrency Primitives
  THREAD_SPAWN,     // Spawn new thread with function: thread { ... }
  THREAD_JOIN,      // Join thread and wait for completion
  THREAD_SEND,      // Send message to thread
  THREAD_RECEIVE,   // Receive message from thread
  
  INTERVAL_START,   // Start interval timer: interval ms { ... }
  INTERVAL_STOP,    // Stop interval timer
  
  TIMEOUT_START,    // Start one-shot timeout: timeout ms { ... }
  TIMEOUT_CANCEL,   // Cancel pending timeout
  
 // Coroutines
 YIELD, // Yield from coroutine (optional value/delay)
 YIELD_RESUME, // Resume yielded coroutine
 GO_ASYNC, // Spawn async function call: go func()
 FIBER_AWAIT, // <- expr: await/blocking expression for fibers
 FIBER_SLEEP, // <-sleep(ms): non-blocking sleep in coroutines
  
// Channels
CHANNEL_NEW, // Create new channel: channel()
CHANNEL_SEND, // Send value to channel
CHANNEL_RECEIVE, // Receive value from channel (blocking)
CHANNEL_CLOSE, // Close channel

// Defer
DEFER_PUSH, // Push a closure onto the current scope's defer stack

// WaitGroup
WAITGROUP_NEW, // Create new WaitGroup: waitgroup()
WAITGROUP_ADD, // WaitGroup.add(n): increment counter
WAITGROUP_DONE, // WaitGroup.done(): decrement counter
WAITGROUP_WAIT, // WaitGroup.wait(): block until counter == 0

    // Module context
    BEGIN_MODULE, // Begin module scope, collect exports to object on stack
    END_MODULE, // End module scope, filter private vars, return exports

    // Math intrinsics (thin wrappers over libm)
    MATH_SIN, MATH_COS, MATH_TAN,
    MATH_ASIN, MATH_ACOS, MATH_ATAN, MATH_ATAN2,
    MATH_SINH, MATH_COSH, MATH_TANH,
    MATH_SQRT, MATH_LOG, MATH_LOG2, MATH_LOG10, MATH_EXP,
    MATH_CEIL, MATH_FLOOR, MATH_ROUND, MATH_ABS,

    // Object intrinsics
    OBJECT_FREEZE, OBJECT_SEAL, OBJECT_IS_FROZEN, OBJECT_IS_SEALED,
    OBJECT_SIZE, OBJECT_ASSIGN,

    // String intrinsics
    STRING_REVERSE, STRING_REPEAT,
    STRING_TRIM_START, STRING_TRIM_END,
    STRING_INCLUDES, STRING_PAD_START, STRING_PAD_END,

    // Bit intrinsics
    BIT_POPCOUNT, BIT_CTZ, BIT_CLZ, BIT_BSWAP, BIT_ROTL, BIT_ROTR,

    // Time primitive
    TIME_NOW,

    // Format intrinsics
    FORMAT_HEX, FORMAT_UNHEX,
    FORMAT_BASE64_ENCODE, FORMAT_BASE64_DECODE,

    CALL_IF_FUNCTION, // Call if value is callable (auto-call bare functions in statement/pipe)
    NOP
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

// FunctionObject: represents a function with optional properties
// Supports fn.prop = value for static state, memoization, etc.
struct FunctionObject {
  uint32_t function_index = 0;
  ObjectRef properties; // User-defined properties attached to the function
};

struct StringRef {
  uint32_t id = 0;
};

struct StringCursorRef {
  uint32_t id = 0;
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

    struct BoundMethodRef {
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

// Concurrency object references
struct ThreadRef {
  uint32_t id = 0; // GC object id for thread
};

struct IntervalRef {
  uint32_t id = 0; // GC object id for interval
};

struct TimeoutRef {
  uint32_t id = 0; // GC object id for timeout
};

struct ChannelRef {
  uint32_t id = 0; // GC object id for channel
};

using BytecodeHostFunction =
    std::function<Value(const std::vector<Value> &)>;

struct SourceLocation {
  std::string filename;
  uint32_t line = 0;
  uint32_t column = 0;
  uint32_t length = 0;
};

// Bytecode instruction - defined early for use in vectors
struct Instruction {
  OpCode opcode;
  std::vector<Value> operands;
  std::optional<SourceLocation> location;

  Instruction() : opcode(OpCode::NOP) {}
  Instruction(OpCode op, std::vector<Value> ops = {})
      : opcode(op), operands(std::move(ops)) {}
};

// ===== CFG-based IR: Basic Block, Terminator, Place =====

// Terminator kinds - explicit control flow
enum class TerminatorKind : uint8_t {
  None,              // Fallthrough to next block (only valid for last block)
  Return,            // Return from function
  Jump,              // Unconditional jump to block
  JumpIfFalse,       // Conditional jump: pop bool, jump if false
  JumpIfTrue,        // Conditional jump: pop bool, jump if true
  JumpIfNull,        // Conditional jump: pop value, jump if null
  Throw,             // Throw exception: pop exception value
  CallReturn,        // Call function and return its result (tail call)
  Unreachable,       // Unreachable code (after return/throw)
};

// Terminator - explicit control flow target
struct Terminator {
  TerminatorKind kind = TerminatorKind::None;
  std::vector<uint32_t> targets;  // Target block indices
  std::optional<SourceLocation> location;

  Terminator() = default;
  Terminator(TerminatorKind k) : kind(k) {}
  Terminator(TerminatorKind k, uint32_t target) : kind(k), targets({target}) {}
  Terminator(TerminatorKind k, uint32_t t1, uint32_t t2) : kind(k), targets({t1, t2}) {}
  Terminator(TerminatorKind k, std::optional<SourceLocation> loc) : kind(k), location(loc) {}
  Terminator(TerminatorKind k, uint32_t target, std::optional<SourceLocation> loc) : kind(k), targets({target}), location(loc) {}
  
  static Terminator ret(std::optional<SourceLocation> loc = {}) {
    Terminator t(TerminatorKind::Return);
    t.location = loc;
    return t;
  }
  static Terminator jump(uint32_t target, std::optional<SourceLocation> loc = {}) {
    Terminator t(TerminatorKind::Jump, target);
    t.location = loc;
    return t;
  }
  static Terminator jumpIfFalse(uint32_t target, std::optional<SourceLocation> loc = {}) {
    Terminator t(TerminatorKind::JumpIfFalse, target);
    t.location = loc;
    return t;
  }
  static Terminator jumpIfTrue(uint32_t target, std::optional<SourceLocation> loc = {}) {
    Terminator t(TerminatorKind::JumpIfTrue, target);
    t.location = loc;
    return t;
  }
  static Terminator jumpIfNull(uint32_t target, std::optional<SourceLocation> loc = {}) {
    Terminator t(TerminatorKind::JumpIfNull, target);
    t.location = loc;
    return t;
  }
  static Terminator throw_(std::optional<SourceLocation> loc = {}) {
    Terminator t(TerminatorKind::Throw);
    t.location = loc;
    return t;
  }
  static Terminator unreachable(std::optional<SourceLocation> loc = {}) {
    Terminator t(TerminatorKind::Unreachable);
    t.location = loc;
    return t;
  }
};

// Place - LValue abstraction for typed local/upvalue/global access
enum class PlaceKind : uint8_t {
  Local,      // Local variable (slot index)
  Upvalue,    // Captured variable (upvalue index)
  Global,     // Global variable (name)
  Field,      // Field of object/struct (base place + field name)
  ArrayIndex, // Array/string element (base place + index place)
};

struct Place {
  PlaceKind kind = PlaceKind::Local;
  uint32_t index = 0;                    // For Local/Upvalue
  std::string name;                      // For Global/Field
  std::shared_ptr<Place> base;           // For Field/ArrayIndex
  std::shared_ptr<Place> index_place;    // For ArrayIndex
  // Type hint for optimization
  uint64_t type_hint = 0;
  bool has_type_hint = false;
  
  // Factory methods
  static Place local(uint32_t idx, uint64_t type_hint = 0) {
    Place p;
    p.kind = PlaceKind::Local;
    p.index = idx;
    p.type_hint = type_hint;
    p.has_type_hint = type_hint != 0;
    return p;
  }
  static Place upvalue(uint32_t idx, uint64_t type_hint = 0) {
    Place p;
    p.kind = PlaceKind::Upvalue;
    p.index = idx;
    p.type_hint = type_hint;
    p.has_type_hint = type_hint != 0;
    return p;
  }
  static Place global(std::string n, uint64_t type_hint = 0) {
    Place p;
    p.kind = PlaceKind::Global;
    p.name = std::move(n);
    p.type_hint = type_hint;
    p.has_type_hint = type_hint != 0;
    return p;
  }
  static Place field(std::shared_ptr<Place> b, std::string n, uint64_t type_hint = 0) {
    Place p;
    p.kind = PlaceKind::Field;
    p.base = std::move(b);
    p.name = std::move(n);
    p.type_hint = type_hint;
    p.has_type_hint = type_hint != 0;
    return p;
  }
  static Place arrayIndex(std::shared_ptr<Place> b, std::shared_ptr<Place> i, uint64_t type_hint = 0) {
    Place p;
    p.kind = PlaceKind::ArrayIndex;
    p.base = std::move(b);
    p.index_place = std::move(i);
    p.type_hint = type_hint;
    p.has_type_hint = type_hint != 0;
    return p;
  }
};

// Typed local variable info
struct LocalInfo {
  uint64_t type_hint = 0;  // TYPE_HINT_* bits
  bool is_mutable = true;  // false for `val`, true for `var`
  bool is_param = false;
  std::string name;        // For debug
};

// Basic block in CFG
struct BasicBlock {
  uint32_t id = 0;
  std::vector<Instruction> instructions;
  Terminator terminator;
  std::vector<uint32_t> predecessors;  // For reverse traversal
  std::vector<uint32_t> successors;    // Computed from terminator
  std::optional<SourceLocation> location; // Block entry location
  bool is_landing_pad = false;         // Exception handler entry
  
  BasicBlock() = default;
  BasicBlock(uint32_t i) : id(i) {}
  
  // Check if block ends with a terminator
  bool is_terminated() const {
    return terminator.kind != TerminatorKind::None;
  }
  
  // Get target blocks from terminator
  std::vector<uint32_t> get_targets() const {
    return terminator.targets;
  }
  
  // Add instruction to block
  void push(Instruction inst) {
    instructions.push_back(std::move(inst));
  }
  
  // Add multiple instructions
  void extend(const std::vector<Instruction>& insts) {
    instructions.insert(instructions.end(), insts.begin(), insts.end());
  }
};

// Forward declaration for BytecodeFunction (used in FunctionBuilder)
struct BytecodeFunction;

// ===== Builder Monad: BlockAnd<()> =====
// Provides structured CFG construction with type-safe block management

class FunctionBuilder {
  struct Impl;
  std::unique_ptr<Impl> impl;
  
public:
  FunctionBuilder(std::string name, uint32_t param_count, uint32_t local_count);
  ~FunctionBuilder();
  
  // Block management
  uint32_t current_block() const;
  void set_current_block(uint32_t block_id);
  uint32_t create_block(std::optional<SourceLocation> loc = {});
  uint32_t create_landing_pad(std::optional<SourceLocation> loc = {});
  
  // Control flow - these set terminator and return new current block
  uint32_t jump(uint32_t target, std::optional<SourceLocation> loc = {});
  uint32_t jump_if_false(uint32_t target, std::optional<SourceLocation> loc = {});
  uint32_t jump_if_true(uint32_t target, std::optional<SourceLocation> loc = {});
  uint32_t jump_if_null(uint32_t target, std::optional<SourceLocation> loc = {});
  uint32_t ret(std::optional<SourceLocation> loc = {});
  uint32_t throw_(std::optional<SourceLocation> loc = {});
  uint32_t unreachable(std::optional<SourceLocation> loc = {});
  
  // Branching helpers - create blocks and branch
  // Returns pair of (then_block, else_block) for if-else
  std::pair<uint32_t, uint32_t> branch(uint32_t cond_block, uint32_t true_target, uint32_t false_target);
  
  // Loop helpers - returns (header_block, body_block, exit_block)
  std::tuple<uint32_t, uint32_t, uint32_t> loop(
      std::function<void(uint32_t header)> init,
      std::function<void(uint32_t body)> body_fn);
  
  // Switch helper - returns (dispatch_block, cases[], default_block)
  std::tuple<uint32_t, std::vector<uint32_t>, uint32_t> switch_(
      uint32_t value_block, const std::vector<uint64_t>& cases, uint32_t default_target);
  
  // Instruction emission
  void push(Instruction inst);
  void push(OpCode op, std::vector<Value> ops = {}, std::optional<SourceLocation> loc = {});
  
  // Convenience for common patterns
  void load_const(Value val, std::optional<SourceLocation> loc = {});
  void load_var(uint32_t local_idx, std::optional<SourceLocation> loc = {});
  void store_var(uint32_t local_idx, std::optional<SourceLocation> loc = {});
  void load_global(std::string name, std::optional<SourceLocation> loc = {});
  void store_global(std::string name, std::optional<SourceLocation> loc = {});
  void load_upvalue(uint32_t idx, std::optional<SourceLocation> loc = {});
  void store_upvalue(uint32_t idx, std::optional<SourceLocation> loc = {});
  
  // Arithmetic with type hint
  void add(uint64_t type_hint = 0, std::optional<SourceLocation> loc = {});
  void sub(uint64_t type_hint = 0, std::optional<SourceLocation> loc = {});
  void mul(uint64_t type_hint = 0, std::optional<SourceLocation> loc = {});
  void div(uint64_t type_hint = 0, std::optional<SourceLocation> loc = {});
  void mod(uint64_t type_hint = 0, std::optional<SourceLocation> loc = {});
  
  // Comparison
  void eq(std::optional<SourceLocation> loc = {});
  void neq(std::optional<SourceLocation> loc = {});
  void lt(std::optional<SourceLocation> loc = {});
  void lte(std::optional<SourceLocation> loc = {});
  void gt(std::optional<SourceLocation> loc = {});
  void gte(std::optional<SourceLocation> loc = {});
  
  // Logical
  void and_(std::optional<SourceLocation> loc = {});
  void or_(std::optional<SourceLocation> loc = {});
  void not_(std::optional<SourceLocation> loc = {});
  
  // Fast integer ops
  void add_int(std::optional<SourceLocation> loc = {});
  void sub_int(std::optional<SourceLocation> loc = {});
  void mul_int(std::optional<SourceLocation> loc = {});
  void div_int(std::optional<SourceLocation> loc = {});
  void mod_int(std::optional<SourceLocation> loc = {});
  
  // String cursor ops
  void string_cursor_new(std::optional<SourceLocation> loc = {});
  void string_cursor_current(std::optional<SourceLocation> loc = {});
  void string_cursor_advance(std::optional<SourceLocation> loc = {});
  void string_cursor_peek(std::optional<SourceLocation> loc = {});
  void string_cursor_reset(std::optional<SourceLocation> loc = {});
  void string_cursor_get_pos(std::optional<SourceLocation> loc = {});
  void string_cursor_set_pos(std::optional<SourceLocation> loc = {});
  
  // Call
  void call(uint32_t arg_count, std::optional<SourceLocation> loc = {});
  void tail_call(uint32_t arg_count, std::optional<SourceLocation> loc = {});
  
  // Build final function
  BytecodeFunction build();
  
  // Access to blocks for advanced manipulation
  std::vector<BasicBlock>& blocks();
  const std::vector<BasicBlock>& blocks() const;
  
  // Local variable management
  uint32_t add_local(LocalInfo info);
  uint32_t add_upvalue(uint32_t index);
  void set_local_type(uint32_t local_idx, uint64_t type_hint);
  
  // Debug
  void set_location(SourceLocation loc);
};

// ===== CFG Validation =====
struct CFGValidationResult {
  bool valid = true;
  std::vector<std::string> errors;
  std::vector<std::string> warnings;
};

CFGValidationResult validate_cfg(const std::vector<BasicBlock>& blocks, uint32_t entry_block = 0);

// ===== Flatten CFG to Linear Instructions (for interpreter/JIT) =====
// Converts CFG to linear instruction list with computed jump offsets
struct LinearFunction {
  std::vector<Instruction> instructions;
  std::vector<uint32_t> block_start_ips;  // block_id -> instruction index
};

LinearFunction flatten_cfg(const std::vector<BasicBlock>& blocks, uint32_t entry_block = 0);


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
  // Function metadata for introspection
  std::vector<std::string> param_names;  // Parameter names
  std::string source_file;               // Source file path
  uint32_t source_line = 0;              // Definition line number
  bool is_generator = false;             
  bool is_timer_closure = false;
  
  
  mutable std::vector<TypeFeedback> type_feedback;
  mutable uint32_t execution_count = 0;
  mutable bool jit_compiled = false;


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

	BytecodeFunction *getFunctionMutable(uint32_t index) {
		if (index >= functions.size()) return nullptr;
		return &functions[index];
	}

  const std::vector<BytecodeFunction> &getAllFunctions() const {
    return functions;
  }

  size_t getFunctionCount() const { return functions.size(); }

  const std::unordered_map<std::string, uint32_t>& getFunctionIndices() const {
    return function_indices;
  }

  uint32_t getFunctionIndex(const BytecodeFunction *func) const {
    if (!func || functions.empty()) return UINT32_MAX;
    auto offset = static_cast<ptrdiff_t>(func - functions.data());
    if (offset < 0 || offset >= static_cast<ptrdiff_t>(functions.size())) return UINT32_MAX;
    return static_cast<uint32_t>(offset);
  }

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
class __attribute__((visibility("default"))) BytecodeInterpreter {
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
class VM; // Forward declaration
class JITCompiler {
public:
  virtual ~JITCompiler() = default;
  virtual void compileFunction(const BytecodeFunction &func) = 0;
  // Tier-aware compilation contract:
  // tier 1 = baseline/fast compile, tier 2 = optimizing/background compile.
  virtual void compileFunctionTier(const BytecodeFunction &func, uint8_t tier) {
    (void)tier;
    compileFunction(func);
  }
  virtual Value executeCompiled(VM* vm, const std::string &func_name,
                                const std::vector<Value> &args) = 0;
  virtual bool isCompiled(const std::string &func_name) const = 0;
  
  // Debug/diagnostic methods
  virtual void setDebugMode(bool enabled) { (void)enabled; }
  virtual void setDumpIR(bool enabled) { (void)enabled; }
  virtual void setDumpAsmToFile(bool enabled) { (void)enabled; }
  virtual void setShowWarnings(bool enabled) { (void)enabled; }
  virtual void setOptimizationLevel(uint8_t level) { (void)level; }
  // Hot-trace compilation (tiered JIT backedge). Default: no-op.
  virtual void compileTrace(const BytecodeFunction &func, uint32_t start_ip,
                            uint64_t hot_count) {
    (void)start_ip;
    (void)hot_count;
    compileFunction(func);
  }
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
         std::unique_ptr<JITCompiler> jcomp = nullptr,
         bool use_jit = true);

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
