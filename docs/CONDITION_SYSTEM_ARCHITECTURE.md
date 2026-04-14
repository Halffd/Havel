# Havel Condition System Architecture Analysis

**Date:** April 14, 2026  
**Status:** Complete Analysis - Phase 2E-3 Ready  
**Focus:** Bytecode compilation, Fiber execution, condition evaluation, function/module ID system

---

## 1. BYTECODE COMPILER PIPELINE

### 1.1 Pipeline Architecture

```
Source Code
    ↓
┌─────────────────────────────────────────┐
│ STAGE 1: LEXING                         │
│ File: src/havel-lang/lexer/Lexer.hpp    │
│ Output: Tokens                          │
└─────────────────────────────────────────┘
    ↓
┌─────────────────────────────────────────┐
│ STAGE 2: PARSING                        │
│ File: src/havel-lang/parser/Parser.h    │
│ Output: AST (ast::Program)              │
└─────────────────────────────────────────┘
    ↓
┌─────────────────────────────────────────┐
│ STAGE 3: SEMANTIC ANALYSIS & RESOLUTION │
│ File: semantic/SemanticAnalyzer.hpp     │
│ File: semantic/BindingResolver.hpp      │
│ Output: LexicalResolutionResult         │
└─────────────────────────────────────────┘
    ↓
┌─────────────────────────────────────────┐
│ STAGE 4: BYTECODE COMPILATION           │
│ File: ByteCompiler.hpp / ByteCompiler.cpp│
│ File: CodeEmitter.hpp / CodeEmitter.cpp │
│ Output: BytecodeChunk                   │
└─────────────────────────────────────────┘
    ↓
┌─────────────────────────────────────────┐
│ STAGE 5: OPTIMIZATION                   │
│ File: CompilationPipeline.hpp           │
│ - Constant folding                      │
│ - Dead code elimination                 │
│ - Jump optimization                     │
│ - Peephole optimization                 │
└─────────────────────────────────────────┘
    ↓
BytecodeChunk (ready for execution)
```

### 1.2 Key Files and Entry Points

| Component | File | Lines | Purpose |
|-----------|------|-------|---------|
| **CompilationPipeline** | `src/havel-lang/compiler/core/CompilationPipeline.hpp` | 1-150 | Orchestrates entire pipeline |
| **CompilationPipeline#compile()** | `CompilationPipeline.cpp` | 30-70 | Main entry point |
| **ByteCompiler** | `src/havel-lang/compiler/core/ByteCompiler.hpp` | 1-200 | Bytecode generation |
| **ByteCompiler#compile()** | `ByteCompiler.cpp` | 97-150 | Compiles Program→BytecodeChunk |
| **CodeEmitter** | `src/havel-lang/compiler/core/CodeEmitter.hpp` | 20-120 | Instruction emission |
| **CodeEmitter#emit()** | `CodeEmitter.cpp` | 1-50 | Emit individual opcodes |
| **BytecodeIR** | `src/havel-lang/compiler/core/BytecodeIR.hpp` | 1-400 | IR structures (Function, Instruction) |

### 1.3 Condition Compilation Entry Points

#### **For standalone condition:**
```
AST::IfStatement (condition branch)
    ↓
ByteCompiler::compileIfStatement()
    ↓
ByteCompiler::compileExpression(condition)
    ↓
CodeEmitter::emit() ← Generates bytecode for condition
```

**File:** [src/havel-lang/compiler/core/ByteCompiler.cpp](src/havel-lang/compiler/core/ByteCompiler.cpp#L3350)  
**Key Methods:**
- `compileIfStatement()` - Compiles `if (condition) { ... }`
- `compileExpression()` - Recursively compiles expressions including conditions
- `compileTernaryExpression()` - Compiles `condition ? true_value : false_value`

#### **For when statements (reactive watchers):**
```
AST::WhenBlock
    ↓
ByteCompiler::compileWhenBlock()
    ↓
ByteCompiler::compileExpression(condition)
    ↓
CodeEmitter::emit() ← Generates condition bytecode
    ↓
Registers with WatcherRegistry:
  - condition_func_id (function index in BytecodeChunk)
  - condition_ip (instruction pointer = 0, start of function)
  - dependencies (tracked during evaluation)
```

**File:** [src/havel-lang/compiler/core/ByteCompiler.hpp](src/havel-lang/compiler/core/ByteCompiler.hpp#L135)  
**Related:** [src/havel-lang/compiler/core/ByteCompiler.cpp](src/havel-lang/compiler/core/ByteCompiler.cpp) - Search `compileWhenBlock`

### 1.4 Bytecode Generated

**Example:** Simple condition `x > 5`

```
LOAD_VAR    x          # Push variable x onto stack
LOAD_CONST  5          # Push constant 5 onto stack
GT                     # Pop two values, push (x > 5) as boolean
```

**Condition in control flow:**
```
if (x > 5) { ... } else { ... }

0: LOAD_VAR     x
1: LOAD_CONST   5
2: GT                      # Stack now has true/false
3: JUMP_IF_FALSE 10        # Jump to else at instruction 10
4: [TRUE BRANCH]
...
9: JUMP 15                 # Skip to end
10: [FALSE BRANCH]
...
15: [CONTINUE]
```

**Ternary expression:** `x > 5 ? "big" : "small"`
```
0: LOAD_VAR     x
1: LOAD_CONST   5
2: GT
3: JUMP_IF_FALSE 9         # False branch
4: LOAD_CONST   "big"      # True value
5: JUMP         11
9: LOAD_CONST   "small"    # False value
11: [CONTINUE]
```

### 1.5 OpCode Reference

**Control Flow Opcodes for Conditions:**
| OpCode | Hex | Use Case |
|--------|-----|----------|
| `JUMP` | 0x40 | Unconditional jump |
| `JUMP_IF_FALSE` | 0x41 | Branch on false condition |
| `JUMP_IF_TRUE` | 0x42 | Branch on true condition |
| `JUMP_IF_NULL` | 0x43 | Jump if null (for `??` operator) |

**Comparison Opcodes:**
| OpCode | Hex | Use Case |
|--------|-----|----------|
| `EQ` | 0x11 | Equal (`==`) |
| `NEQ` | 0x12 | Not equal (`!=`) |
| `LT` | 0x13 | Less than (`<`) |
| `LTE` | 0x14 | Less than or equal (`<=`) |
| `GT` | 0x15 | Greater than (`>`) |
| `GTE` | 0x16 | Greater than or equal (`>=`) |

**Logical Opcodes:**
| OpCode | Hex | Use Case |
|--------|-----|----------|
| `AND` | 0x17 | Logical AND (`&&`) |
| `OR` | 0x18 | Logical OR (`\|\|`) |
| `NOT` | 0x19 | Logical NOT (`!`) |

---

## 2. FIBER SYSTEM

### 2.1 Fiber Architecture

```
Fiber (Per-Goroutine Execution Context)
├─ IDENTITY
│  ├─ uint32_t id                    # Unique fiber ID
│  └─ std::string name               # Debug name (e.g., "fiber-42")
│
├─ BYTECODE POSITION (CRITICAL FOR RESUMPTION)
│  ├─ uint32_t current_function_id   # Which function is executing
│  ├─ uint32_t current_chunk_index   # Which bytecode chunk (for modules)
│  └─ uint32_t ip                    # Instruction pointer in current function
│
├─ CALL STACK (NESTED FUNCTION CALLS)
│  └─ std::vector<CallFrame> call_stack
│     └─ Each CallFrame has:
│        ├─ uint32_t function_id
│        ├─ uint32_t ip              # IP in that function
│        ├─ uint32_t locals_base     # Where locals start
│        └─ uint32_t arg_count
│
├─ VM STATE (PER-FIBER, NOT GLOBAL)
│  ├─ FiberStack stack               # Operand stack (independent)
│  ├─ std::map<string, Value> locals # Local variables
│  └─ Value return_value
│
├─ EXECUTION STATE
│  ├─ FiberState state               # CREATED, RUNNABLE, RUNNING, SUSPENDED, DONE
│  └─ SuspensionReason suspended_reason
│
├─ SUSPENSION CONTEXT
│  ├─ void* suspension_context       # What it's waiting for
│  └─ uint64_t suspension_timestamp
│
└─ METADATA
   ├─ uint64_t created_time
   ├─ uint32_t parent_id
   ├─ size_t max_stack_depth
   └─ error handling fields
```

### 2.2 Fiber State Machine

```
┌──────────────┐
│   CREATED    │ (freshly spawned)
└──────┬───────┘
       │
       ↓
┌──────────────┐    (ready to run)
│  RUNNABLE    │◄─────────┐
└──────┬───────┘          │
       │                  │
       ↓                  │ resume()
┌──────────────┐          │
│   RUNNING    │ ─────────┤ (currently executing)
└──────┬───────┘          │
       │                  │
   ┌───┴──────────────┬──────────────┬────────────┐
   │                  │              │            │
   ↓                  ↓              ↓            ↓
RETURN          YIELD/SUSPEND    ERROR      (fall off end)
   │                  │              │            │
   ├─→┌──────────────┐│              │            │
   │  │   DONE       │└──→┌──────────┴───────────┴─→ DONE
   │  └──────────────┘
   │
   └──→ DONE
```

### 2.3 Fiber Creation and Resumption

**Create:**
```cpp
// File: src/havel-lang/runtime/concurrency/Fiber.hpp:217-250
Fiber fiber(
    uint32_t fiber_id,           // 0, 1, 2, ...
    uint32_t start_function_id,  // Which function to start in
    uint32_t parent_fiber_id,    // Parent fiber (for spawning hierarchy)
    std::string fiber_name       // Debug name
);

// Constructor initializes:
// - id, name ← parameters
// - current_function_id ← start_function_id
// - current_chunk_index ← 0
// - ip ← 0
// - call_stack ← [CallFrame(start_function_id, 0, 0)]
// - state ← CREATED
// - stack, locals ← empty
```

**Resume (after suspension):**
```cpp
// File: src/havel-lang/runtime/concurrency/Fiber.hpp:316-350
fiber.resume();
// Transitions: SUSPENDED → RUNNABLE
// ✅ IP is preserved in currentFrame() (the top call frame)
// ✅ Stack and locals survive suspension
// ✅ Ready to continue from exactly where it paused
```

### 2.4 Call Frame Management

**Push call (function call):**
```cpp
fiber.pushCall(
    uint32_t function_id,  // Function to call
    uint32_t arg_count     // Arguments passed
);
```

**Pop call (return):**
```cpp
fiber.popCall();  // Exit current function
// IP restored from new top frame
```

### 2.5 Stack and Locals Storage

**FiberStack (per-fiber operand stack):**
```cpp
// File: src/havel-lang/runtime/concurrency/Fiber.hpp:73-114
class FiberStack {
    std::vector<Value> data_;  // Actual values
    size_t sp_;                // Stack pointer
    
public:
    void push(const Value& v);  // Push value
    Value pop();                // Pop and return
    Value peek(size_t depth = 1) const;  // Peek without removing
    void set(size_t depth, const Value& v);  // Modify stack value
    size_t size() const;        // Current stack depth
};

// Independent per-fiber: each fiber's stack survives suspension
```

**Locals (local variables):**
```cpp
std::map<std::string, Value> locals;  // In Fiber class

// Accessed via:
// - LOAD_VAR <var_name>  ← loads from fiber.locals[name]
// - STORE_VAR <var_name> ← stores to fiber.locals[name]
```

### 2.6 Fiber Integration with Scheduler

```
Scheduler (manages goroutines)
    │
    ├─ ready_queue: [Fiber*, ...]       # RUNNABLE
    ├─ suspended_by_reason: map         # SUSPENDED fibers grouped by reason
    └─ done_list: [Fiber*, ...]         # DONE fibers
        
VM::executeOneStep(Fiber* fiber)
    │
    ├─ Read fiber.ip
    ├─ Execute instruction at that IP
    ├─ Increment fiber.ip
    ├─ Check for suspension (yield, channel, etc.)
    └─ Save state to fiber via saveFiberState()

saveFiberState(Fiber* fiber):
    ├─ Copy VM frame state → fiber.call_stack
    ├─ Copy VM stack → fiber.stack
    └─ Copy VM locals → fiber.locals
```

### 2.7 Fiber Suspension and GC

**Suspended Fiber Protection:**
```cpp
// File: src/havel-lang/runtime/concurrency/Fiber.hpp:367-385
std::vector<Value> getGCRoots() const {
    // Returns all Values that must be protected
    // - Stack values
    // - Local variables
    // - Return value
    // GC marks all these as roots when fiber is suspended
}

// When suspended:
// - VM.stack can be cleared (no longer needed)
// - Fiber.stack and fiber.locals are GC roots
// - GC must scan getGCRoots() to prevent collection
```

---

## 3. EVALUATOR CALLBACKS & CONDITION EVALUATION

### 3.1 Condition Evaluation Flow

```
WHEN STATEMENT ACTIVATION
│
├─ COMPILE TIME:
│  ├─ ByteCompiler::compileWhenBlock()
│  │  ├─ Extracts condition expression
│  │  ├─ Compiles to bytecode (condition_func_id)
│  │  ├─ Tracks dependencies (which variables used)
│  │  └─ Returns: {condition_func_id, condition_ip=0, dependencies}
│  │
│  └─ WatcherRegistry::registerWatcher()
│     └─ Stores: [condition_func_id, condition_ip, dependencies, fiber]
│
├─ RUNTIME:
│  │
│  ├─ Variable Change Event:
│  │  └─ onVariableChanged("x") event fires
│  │
│  ├─ ExecutionEngine::onVariableChanged():
│  │  └─ Notifies all watchers that depend on "x"
│  │
│  ├─ WatcherRegistry::onVariableChanged():
│  │  └─ For each affected watcher:
│  │     ├─ Call evaluator callback
│  │     │  └─ ExecutionEngine::evaluateCondition(watcher_id)
│  │     │     └─ VM::evaluateConditionBytecode(func_id, ip=0)
│  │     │        └─ Execute condition bytecode
│  │     │           └─ Return boolean result
│  │     │
│  │     └─ Check false→true edge:
│  │        ├─ If (was_false && now_true) → FIRE
│  │        │  └─ Resume fiber (add to resume list)
│  │        │
│  │        └─ Update last_result
│  │
│  └─ ExecutionEngine::executeFrame()
│     └─ Resume fired fibers
└─ Fiber continues from suspension point
```

### 3.2 VM Condition Bytecode Evaluation

**File:** [src/havel-lang/compiler/vm/VM.cpp](src/havel-lang/compiler/vm/VM.cpp#L2414-L2440)  
**File:** [src/havel-lang/compiler/vm/VM.hpp](src/havel-lang/compiler/vm/VM.hpp#L332)

```cpp
// PHASE 2E: Evaluate condition bytecode
bool VM::evaluateConditionBytecode(uint32_t func_index, uint32_t ip = 0) {
    // Parameters:
    // - func_index: Function ID in current_chunk->functions[]
    // - ip: Starting instruction (default 0, start of function)
    //
    // Process:
    // 1. Get function from current_chunk (set by compile())
    // 2. Create DependencyTrackerScope (automatic variable tracking)
    // 3. Create lightweight execution context
    //    - Save current VM stack (conditions shouldn't affect main VM)
    //    - Execute function bytecode
    //    - Get top of stack as boolean result
    //    - Restore stack
    // 4. Return result: true/false
    //
    // Usage:
    // - Called from ExecutionEngine::evaluateCondition()
    // - Takes current thread/fiber context
    // - Returns immediately (non-blocking)
    //
    // ✅ Already tracks variable accesses via g_active_tracker
}
```

**Key Methods:**
| Method | File | Purpose |
|--------|------|---------|
| `VM::evaluateConditionBytecode()` | `VM.cpp:2414` | Execute condition bytecode |
| `VM::call()` | `VM.cpp` | Call function and execute it |
| `ExecutionEngine::evaluateCondition()` | `ExecutionEngine.cpp:251` | Wrapper with watcher lookup |
| `WatcherRegistry::onVariableChanged()` | `WatcherRegistry.hpp:48+` | Notify and re-evaluate |

### 3.3 Dependency Tracking

**File:** [src/havel-lang/runtime/concurrency/DependencyTracker.hpp](src/havel-lang/runtime/concurrency/DependencyTracker.hpp)

```cpp
// PHASE 2B: DependencyTracker
// ─────────────────────────────────────────────────────────────

class DependencyTracker {
    // Called during condition evaluation:
    void trackGlobalAccess(const std::string& var_name);
    void trackLocalAccess(const std::string& var_name);
    
    // After evaluation, get what was accessed:
    std::unordered_set<std::string> getDependencies() const;
};

// USAGE:
// ──────
// 1. Create tracker:
//    auto tracker = std::make_shared<DependencyTracker>();
//
// 2. Set active (RAII guard):
//    DependencyTrackerScope scope(tracker);  // Sets g_active_tracker
//
// 3. Evaluate condition (inside scope):
//    bool result = vm_->evaluateConditionBytecode(func_id, 0);
//    // During bytecode execution, LOAD_VAR calls track access
//
// 4. Get dependencies (after scope ends):
//    auto deps = tracker->getDependencies();
//    // Result: {"x", "y"} if condition accessed variables x and y
//
// 5. Register watcher with dependencies:
//    watcher_registry_->registerWatcher(
//        func_id, 0, result, deps, fiber
//    );
//    // Now watcher only re-evaluates when x or y changes
```

**Automatic Tracking:**
```cpp
// Inside VM::LOAD_VAR implementation:
Value VM::getGlobal(const std::string& name) {
    // ... get value ...
    trackGlobalAccess(name);  // ← Notifies DependencyTracker
    return value;
}

// Inside VM::LOAD_VAR for locals:
Value VM::getLocal(const std::string& name) {
    // ... get value ...
    trackLocalAccess(name);  // ← Notifies DependencyTracker
    return value;
}
```

### 3.4 WatcherRegistry Condition Re-evaluation

**File:** [src/havel-lang/runtime/concurrency/WatcherRegistry.hpp](src/havel-lang/runtime/concurrency/WatcherRegistry.hpp#L48+)

```cpp
// WatcherRegistry::onVariableChanged()
// ────────────────────────────────────
// Called when variable changes (VAR_CHANGED event)
//
// Signature:
std::vector<Fiber*> onVariableChanged(
    const std::string& var_name,
    // Function to re-evaluate condition
    std::function<bool(uint32_t watcher_id)> evaluator
);

// Algorithm:
// 1. Find all watchers that depend on var_name
// 2. For each watcher:
//    a. Call evaluator(watcher_id)
//       → ExecutionEngine::evaluateCondition(watcher_id)
//       → VM::evaluateConditionBytecode(func_id, ip)
//       → Returns boolean (new condition result)
//    b. Check false→true edge transition:
//       if (was_false && now_true) {
//           fired_fibers.push_back(fiber);  // Will be resumed
//       }
//    c. Update last_result
// 3. Return list of fibers to resume
```

---

## 4. MODULE/FUNCTION ID SYSTEM

### 4.1 Function ID Assignment

```
Compilation Process:
    │
    ├─ FOR EACH FUNCTION:
    │  ├─ Assign index: function_id = functions.size()
    │  ├─ Add to chunk: chunk->addFunction(func)
    │  └─ Register mapping: ast_node → function_id
    │
    └─ Result:
       ├─ Function 0: "__main__" (entry point)
       ├─ Function 1: "add(a, b)"
       ├─ Function 2: "foo(x)"
       ├─ Function 3: "<lambda at line 42>"
       └─ ...
```

**File:** [src/havel-lang/compiler/core/ByteCompiler.cpp](src/havel-lang/compiler/core/ByteCompiler.cpp)

```cpp
// ByteCompiler compiles top-level functions:
void compileFunction(const ast::FunctionDeclaration& function) {
    // Create bytecode function
    BytecodeFunction bf;
    bf.name = function.name;
    bf.param_count = function.params.size();
    bf.local_count = ...;
    
    // Get function index
    uint32_t function_id = chunk->getFunctionCount();
    
    // Register mapping (for later lookup)
    function_indices_by_node_[&function] = function_id;
    
    // Begin function context
    enterFunction(std::move(bf), function_id);
    
    // Compile function body
    for (const auto& stmt : function.body) {
        compileStatement(*stmt);
    }
    
    // End and add to chunk
    uint32_t index = leaveFunction();  // Returns function_id
}
```

### 4.2 Function ID Lookup

**Direct ID to Function:**
```cpp
// Files: BytecodeIR.hpp

class BytecodeChunk {
public:
    const BytecodeFunction* getFunction(uint32_t index) const {
        if (index >= functions.size()) return nullptr;
        return &functions[index];
    }
    
    const BytecodeFunction* getFunction(const std::string& name) const {
        auto it = function_indices.find(name);
        return it != function_indices.end() 
            ? &functions[it->second] 
            : nullptr;
    }
    
    // Storage:
    std::vector<BytecodeFunction> functions;
    std::unordered_map<std::string, uint32_t> function_indices;
};
```

### 4.3 Instruction Pointer (IP) Usage

```
Instruction Pointer (IP) = Bytecode offset within a function
    │
    ├─ Range: 0 to function.instructions.size()-1
    │
    ├─ Stored in:
    │  ├─ Fiber.ip (global for entire fiber)
    │  └─ CallFrame.ip (for each nested function call)
    │
    └─ Example:
       Function "add": [
           0: LOAD_VAR a      ← IP 0
           1: LOAD_VAR b      ← IP 1
           2: ADD             ← IP 2
           3: RETURN          ← IP 3
       ]
       
       If fiber pauses at IP=2:
       - fiber.ip = 2
       - On resume, execution continues with ADD instruction
```

### 4.4 Bytecode Chunk Structure

**File:** [src/havel-lang/compiler/core/BytecodeIR.hpp](src/havel-lang/compiler/core/BytecodeIR.hpp#L239-400)

```cpp
class BytecodeChunk {
    // Functions (indexed by function_id)
    std::vector<BytecodeFunction> functions;              // functions[0], functions[1], ...
    std::unordered_map<std::string, uint32_t> function_indices;  // "add" → 1
    
    // Metadata
    std::string module_name;
    std::vector<std::string> imports;
    
    // Methods:
    uint32_t getFunctionCount() const { return functions.size(); }
    const BytecodeFunction* getFunction(uint32_t index) const;
    const BytecodeFunction* getFunction(const std::string& name) const;
    void addFunction(BytecodeFunction func);
};

struct BytecodeFunction {
    std::string name;                          // "add", "foo", "__main__", etc.
    uint32_t param_count;                      // Number of parameters
    uint32_t local_count;                      // Number of local variables
    std::vector<Instruction> instructions;     // The bytecode
    std::vector<Value> constants;              // Constant pool (per-function now)
    std::vector<std::string> param_names;      // Parameter names
};

struct Instruction {
    OpCode opcode;                             // What to do (e.g., LOAD_VAR)
    std::vector<Value> operands;               // Arguments (e.g., variable name)
    std::optional<SourceLocation> location;    // Source location for debugging
};
```

### 4.5 Reference to Bytecode in Watchers

**For `when` statements:**

```havel
let counter = 0;

when counter > 5 {
    print "counter exceeded 5!";
}
```

**Compilation→Registration:**
```cpp
// ByteCompiler::compileWhenBlock()
// ────────────────────────────────

// 1. Compile condition expression into separate function
uint32_t condition_func_id = chunk->getFunctionCount();
BytecodeFunction condition_fn;
condition_fn.name = "when_condition_42";  // Auto-generated
// ... compile "counter > 5" into instructions ...
chunk->addFunction(condition_fn);  // Now function_id = N

// 2. Extract dependencies (which variables are accessed)
auto tracker = std::make_shared<DependencyTracker>();
{
    DependencyTrackerScope scope(tracker);
    // Simulate evaluation to find what variables are used
    // This records any LOAD_VAR and LOAD_GLOBAL operations
}
auto dependencies = tracker->getDependencies();
// Result: {"counter"}

// 3. Register with WatcherRegistry
watcher_id = watcher_registry_->registerWatcher(
    condition_func_id,              // Which function has condition bytecode
    0,                              // Start at IP 0 (beginning of function)
    false,                          // Initial evaluation result
    {"counter"},                    // Dependencies tracked
    current_fiber                   // Fiber to resume on fire
);
```

**Runtime Re-evaluation:**

```cpp
// When counter = 10 (VAR_CHANGED event):

// ExecutionEngine::onVariableChanged("counter"):
auto fired_fibers = watcher_registry_->onVariableChanged(
    "counter",
    [this](uint32_t watcher_id) {
        return evaluateCondition(watcher_id);
    }
);

// ExecutionEngine::evaluateCondition(watcher_id):
// 1. Get watcher from registry
const auto* watcher = watcher_registry_->getWatcher(watcher_id);
// watcher->condition_func_id = N (condition function)
// watcher->condition_ip = 0

// 2. Re-evaluate condition bytecode
bool result = vm_->evaluateConditionBytecode(
    watcher->condition_func_id,  // N
    watcher->condition_ip        // 0
);
// Executes function N from IP 0:
//   0: LOAD_VAR counter    → push 10
//   1: LOAD_CONST 5        → push 5
//   2: GT                  → pop 10, 5 → push true
//   3: RETURN              → pop true, return

// result = true (10 > 5)

// 3. Check edge transition:
if (was_false && now_true) {
    fired_fibers.push_back(fiber);  // Resume this fiber
}
```

---

## 5. ARCHITECTURE DIAGRAMS

### 5.1 End-to-End Condition Flow

```
SOURCE CODE                      COMPILATION                     RUNTIME
───────────────────────────────────────────────────────────────────────────

when x > 5 {
    print "big";
}

    │                                │
    ├─ Parse WhenBlock              │
    │                                │
    ├─ CreateConditionFunction      │
    │  when_condition_1 {            │
    │      return x > 5;             │
    │  }                             │
    │                                │
    ├─ Compile to bytecode:          │ CompileCondition()
    │  0: LOAD_GLOBAL x              │ ┌──────────────╖
    │  1: LOAD_CONST 5               │ │ func_id = 10 ║
    │  2: GT                         │ │ ip = 0       ║
    │  3: RETURN                     │ │ deps={x}     ║
    │                                │ └──────────────╜
    │                                │      │
    │                                │ RegisterWatcher()
    │                                │ ┌──────────────────────╖
                                      │ watcher_id = 42      │
                                      │ condition_func_id=10 │
                                      │ condition_ip = 0     │
                                      │ last_result = false  │
    │                                │ │ dependencies = {x}   │
    │                                │ │ fiber = fiber #3     │
    │                                │ └──────────────────────╜
    │                                │
    │                                │          EVENT
    │                                │
    │                                │ USER CODE:
    │                                │   x = 10
    │                                │
    │                                │ VAR_CHANGED event fires
    │                                │      │
    │                                │ WatcherRegistry checks:
    │                                │   "x" in dependencies?
    │                                │   YES → Re-evaluate
    │                                │
    │                                │ ExecutionEngine::evaluateCondition(42)
    │                                │      │
    │                                │ VM::evaluateConditionBytecode(10, 0)
    │                                │      │
    │                                ├─ Execute bytecode:
    │                                │  0: LOAD_GLOBAL x → stack=[10]
    │                                │  1: LOAD_CONST 5  → stack=[10, 5]
    │                                │  2: GT            → stack=[true]
    │                                │  3: RETURN        → return true
    │                                │
    │                                ├─ Check edge:
    │                                │  was_false (false) && now_true (true)?
    │                                │  YES → FIRE
    │                                │
    │                                ├─ ExecutionEngine::executeFrame()
    │                                │  Resume fiber #3
    │                                │      │
    │                                └─────► Fiber #3 continues
                                      │  print "big"
```

### 5.2 Fiber State Transitions During Execution

```
FIBER LIFECYCLE
─────────────────────────────────────────────────────────────

CREATE:
  new Fiber(42, 0)  // id=42, start in function 0
     │
     ├─ state = CREATED
     ├─ call_stack = [CallFrame(0, 0)]  // Start in function 0 at IP 0
     ├─ stack = []
     ├─ locals = {}
     │
     v

SCHEDULER::enqueue(fiber)
     │
     ├─ Add to ready_queue
     ├─ state = RUNNABLE
     │
     v

MAIN LOOP: ExecutionEngine::executeFrame()
     │
     ├─ Drain EventQueue
     ├─ Pick next runnable: fiber #42
     ├─ state = RUNNING
     │
     ├─ VM::executeOneStep(fiber)
     │  ├─ Read fiber.ip (current instruction)
     │  ├─ Execute instruction
     │  │  - Modifies fiber.stack and fiber.locals
     │  │  - May increment fiber.ip
     │  │
     │  └─ Check suspension reasons:
     │     ├─ YIELD?
     │     │  ├─ state = SUSPENDED
     │     │  ├─ suspension_reason = YIELD
     │     │  └─ Return to scheduler (cooperative switch)
     │     │
     │     ├─ CHANNEL_RECV?
     │     │  ├─ state = SUSPENDED
     │     │  ├─ suspension_reason = CHANNEL_RECV
     │     │  ├─ suspension_context = &channel
     │     │  └─ Event handler wakes when data available
     │     │
     │     ├─ RETURN?
     │     │  ├─ fiber.popCall()
     │     │  ├─ More frames? → Continue
     │     │  ├─ No frames?  → state = DONE
     │     │  └─ return_value set
     │     │
     │     └─ ERROR?
     │        ├─ state = DONE (with error)
     │        └─ error_message set
     │
     └─ Save fiber state:
        ├─ saveFiberState(fiber)
        ├─ Copy VM frame → fiber.call_stack
        ├─ Copy VM stack → fiber.stack
        └─ Copy VM locals → fiber.locals
```

### 5.3 Condition Evaluation Dependency Flow

```
WHEN STATEMENT COMPILATION
──────────────────────────────────────────────────────

ByteCompiler::compileWhenBlock()
  │
  ├─ Extract condition: Counter > 5
  │
  ├─ Create condition function:
  │    fn when_cond_1() {
  │        Counter > 5
  │    }
  │
  ├─ Compile to bytecode (function_id = 10):
  │    0: LOAD_VAR    Counter
  │    1: LOAD_CONST  5
  │    2: GT
  │    3: RETURN
  │
  ├─ DEPENDENCY TRACKING:
  │
  │    Create DependencyTracker
  │    Set active: g_active_tracker = tracker
  │
  │    During compilation/simulation:
  │    - LOAD_VAR Counter → tracker.trackGlobalAccess("Counter")
  │
  │    Get dependencies: {"Counter"}
  │
  ├─ Register watcher:
  │
  │    watcher_registry_.registerWatcher(
  │        10,              // condition_func_id
  │        0,               // condition_ip
  │        false,           // initial eval
  │        {"Counter"},     // depends on Counter
  │        fiber_ptr        // which fiber to wake
  │    )
  │
  └─ Returns: watcher_id = 42


RUNTIME VARIABLE CHANGE
──────────────────────────────────────────────────────

User code: Counter = 42

VM::STORE_GLOBAL("Counter") / STORE_VAR("Counter")
  │
  ├─ Store value to global/local scope
  │
  ├─ Post event: VAR_CHANGED("Counter")
  │  └─ Queued in event loop
  │
  └─ Continue execution
  

ExecutionEngine::executeFrame()
  │
  ├─ processEventQueue()
  │  │
  │  ├─ Event: VAR_CHANGED("Counter")
  │  │  │
  │  │  └─ Call onVariableChanged("Counter"):
  │  │
  │  │     WatcherRegistry::onVariableChanged("Counter")
  │  │       │
  │  │       ├─ Find all watchers depending on "Counter"
  │  │       │  → [watcher_id = 42]
  │  │       │
  │  │       ├─ For watcher 42:
  │  │       │
  │  │       │  Call evaluator(42):
  │  │       │    ExecutionEngine::evaluateCondition(42)
  │  │       │      │
  │  │       │      ├─ Get watcher: {func_id=10, ip=0, deps={Counter}}
  │  │       │      │
  │  │       │      ├─ Create DependencyTrackerScope
  │  │       │      │  (optional, may re-track dependencies)
  │  │       │      │
  │  │       │      └─ Call VM::evaluateConditionBytecode(10, 0):
  │  │       │         │
  │  │       │         ├─ Execute function 10 from IP 0:
  │  │       │         │  0: LOAD_VAR Counter  → push 42
  │  │       │         │  1: LOAD_CONST 5      → push 5
  │  │       │         │  2: GT                → pop, pop, push (42>5=true)
  │  │       │         │  3: RETURN            → pop true, return
  │  │       │         │
  │  │       │         └─ Return: true
  │  │       │
  │  │       ├─ Check edge: was_false (false) && now_true (true)?
  │  │       │  → YES → ADD FIBER TO RESUME LIST
  │  │       │
  │  │       └─ Update watcher: last_result = true
  │  │
  │  └─ EventQueue processed
  │
  └─ Pick next runnable → fiber that was resumed
```

---

## 6. IMPLEMENTATION STATUS

### 6.1 What's Already Implemented ✅

| Component | Status | File | Lines |
|-----------|--------|------|-------|
| **Pipeline** | ✅ Complete | `CompilationPipeline.hpp` | 1-150 |
| **ByteCompiler** | ✅ Complete | `ByteCompiler.hpp/cpp` | ~4000 |
| **CodeEmitter** | ✅ Complete | `CodeEmitter.hpp/cpp` | ~500 |
| **Fiber class** | ✅ Complete | `Fiber.hpp` | 1-400 |
| **FiberStack** | ✅ Complete | `Fiber.hpp:73-114` | 42 lines |
| **CallFrame** | ✅ Complete | `Fiber.hpp:30-70` | 40 lines |
| **Fiber state machine** | ✅ Complete | `Fiber.hpp:135-165` | 30 lines |
| **Fiber suspend/resume** | ✅ Complete | `Fiber.hpp:316-350` | 35 lines |
| **Fiber GC support** | ✅ Complete | `Fiber.hpp:367-385` | 20 lines |
| **VM::evaluateConditionBytecode()** | ✅ Complete | `VM.cpp:2414` | ~30 lines |
| **DependencyTracker** | ✅ Complete | `DependencyTracker.hpp` | ~100 lines |
| **DependencyTrackerScope** | ✅ Complete | `DependencyTracker.hpp` | ~50 lines |
| **WatcherRegistry** | ✅ Complete | `WatcherRegistry.hpp` | ~150 lines |
| **ExecutionEngine** | ✅ Complete | `ExecutionEngine.hpp/cpp` | ~200 lines |
| **saveFiberState()** | ✅ Complete | `VM.cpp:2784` | ~40 lines |
| **BytecodeChunk** | ✅ Complete | `BytecodeIR.hpp:345` | ~40 lines |
| **BytecodeFunction** | ✅ Complete | `BytecodeIR.hpp` | ~60 lines |
| **OpCode enums** | ✅ Complete | `BytecodeIR.hpp` | ~150 lines |

### 6.2 What Needs Implementation ❌

| Component | Priority | Notes |
|-----------|----------|-------|
| **Watcher condition firing body** | HIGH | When condition fires, execute the when-block body bytecode |
| **Integration test** | HIGH | End-to-end test of `when` statement firing |
| **Event loop integration** | MEDIUM | VAR_CHANGED events need proper emitting from all variable stores |
| **Channel suspension** | MEDIUM | CHANNEL_SEND/CHANNEL_RECV suspension reasons |
| **Thread join suspension** | MEDIUM | THREAD_JOIN suspension reason |
| **Timer integration** | MEDIUM | TIMER suspension reason |
| **Debugger breakpoint conditions** | LOW | Conditional breakpoints using condition bytecode |

### 6.3 Entry Points for Modification

#### To add custom condition evaluation:
- **File:** [src/havel-lang/compiler/core/ByteCompiler.cpp](src/havel-lang/compiler/core/ByteCompiler.cpp)
- **Method:** `ByteCompiler::compileWhenBlock()`
- **Line:** Search for "compileWhenBlock"

#### To add custom variable tracking:
- **File:** [src/havel-lang/runtime/concurrency/DependencyTracker.hpp](src/havel-lang/runtime/concurrency/DependencyTracker.hpp)
- **Method:** `trackGlobalAccess()`, `trackLocalAccess()`
- **Integration:** Call from `VM::LOAD_VAR`, `VM::LOAD_GLOBAL`

#### To add watcher firing behavior:
- **File:** [src/havel-lang/runtime/execution/ExecutionEngine.cpp](src/havel-lang/runtime/execution/ExecutionEngine.cpp)
- **Method:** Search for "fired_fibers" or "onVariableChanged"
- **Location:** `ExecutionEngine::onVariableChanged()` line ~234

#### To add suspend/resume events:
- **File:** [src/havel-lang/compiler/vm/VM.cpp](src/havel-lang/compiler/vm/VM.cpp)
- **Method:** `VM::executeOneStep()`
- **Suspension cases:** YIELD, CHANNEL_RECV, CHANNEL_SEND, etc.

---

## 7. KEY INSIGHTS AND PATTERNS

### 7.1 Bytecode Condition Execution Pattern

```
Conditions are NOT evaluated eagerly during compilation.
Instead:
1. Compiled into bytecode functions
2. Bytecode executed at runtime (on variable changes)
3. Execution is immediate/non-blocking
4. Uses current VM context (globals accessible)
```

### 7.2 Fiber Resume Chain

```
on Fiber suspension:
  ├─ State frozen (IP, stack, locals, call_stack)
  ├─ Fiber removed from execution
  └─ Stored in suspension queue

on trigger event:
  ├─ Scheduler.ready_queue.push(fiber)
  ├─ Fiber.state = RUNNABLE
  ├─ Main loop picks it next
  └─ Execution resumes from saved IP
```

### 7.3 Function ID Portability

```
Function IDs are STABLE across:
  ✅ Suspension/resumption
  ✅ Different fibers
  ✅ Module reloading (if rebuilding chunk)

Function IDs are NOT stable across:
  ❌ Function recompilation
  ❌ Adding/removing functions before this one
```

### 7.4 Dependency Tracking Limitations

```
Current tracking:
  ✅ Records variable names accessed
  ✅ Works for direct access (x, y)
  ❌ Doesn't track array element changes (x[0])
  ❌ Doesn't track object field changes (x.field)
  ⚠️  May over-report for function calls that don't access vars
```

---

## 8. COMPILATION PIPELINE WALKTHROUGH

1. **Lexing:** Source code → tokens
2. **Parsing:** Tokens → AST (including WhenBlock nodes)
3. **Semantic Analysis:** AST validation, type checking
4. **Bytecode Compilation:**
   - For each WhenBlock:
     - Extract condition expression
     - Create BytecodeFunction for condition
     - Emit comparison opcodes (LOAD_VAR, GT, etc.)
     - Assign function_id to condition function
5. **Dependency Extraction:**
   - Simulate condition evaluation
   - Record accessed variables
6. **Watcher Registration:**
   - Call WatcherRegistry::registerWatcher()
   - Store: (func_id, ip=0, dependencies, fiber)
7. **Optimization:**
   - Constant folding on condition bytecode
   - Jump optimization

---

## 9. TESTING CHECKLIST

- [ ] Compile `when x > 5 { ... }` bytecode
- [ ] Verify condition bytecode functions created with correct IDs
- [ ] Register watcher with correct dependencies
- [ ] Trigger VAR_CHANGED("x")
- [ ] Re-evaluate condition bytecode (should return true)
- [ ] Check false→true edge (should fire)
- [ ] Resume fiber from suspension
- [ ] Execute when-block body

---

## 10. FILE SUMMARY

| Path | Lines | Purpose |
|------|-------|---------|
| [src/havel-lang/compiler/core/CompilationPipeline.hpp](src/havel-lang/compiler/core/CompilationPipeline.hpp) | 150 | Pipeline orchestration |
| [src/havel-lang/compiler/core/CompilationPipeline.cpp](src/havel-lang/compiler/core/CompilationPipeline.cpp) | 300+ | Pipeline implementation |
| [src/havel-lang/compiler/core/ByteCompiler.hpp](src/havel-lang/compiler/core/ByteCompiler.hpp) | 200 | Bytecode compiler interface |
| [src/havel-lang/compiler/core/ByteCompiler.cpp](src/havel-lang/compiler/core/ByteCompiler.cpp) | 4000+ | Bytecode generation |
| [src/havel-lang/compiler/core/CodeEmitter.hpp](src/havel-lang/compiler/core/CodeEmitter.hpp) | 120 | Instruction emission |
| [src/havel-lang/compiler/core/CodeEmitter.cpp](src/havel-lang/compiler/core/CodeEmitter.cpp) | 300+ | Instruction emission impl |
| [src/havel-lang/compiler/core/BytecodeIR.hpp](src/havel-lang/compiler/core/BytecodeIR.hpp) | 400 | IR data structures |
| [src/havel-lang/runtime/concurrency/Fiber.hpp](src/havel-lang/runtime/concurrency/Fiber.hpp) | 400 | Fiber definition |
| [src/havel-lang/runtime/concurrency/DependencyTracker.hpp](src/havel-lang/runtime/concurrency/DependencyTracker.hpp) | 100 | Dependency tracking |
| [src/havel-lang/runtime/concurrency/WatcherRegistry.hpp](src/havel-lang/runtime/concurrency/WatcherRegistry.hpp) | 150 | Watcher management |
| [src/havel-lang/compiler/vm/VM.hpp](src/havel-lang/compiler/vm/VM.hpp) | 400 | VM interface |
| [src/havel-lang/compiler/vm/VM.cpp](src/havel-lang/compiler/vm/VM.cpp) | 5000+ | VM implementation |
| [src/havel-lang/runtime/execution/ExecutionEngine.hpp](src/havel-lang/runtime/execution/ExecutionEngine.hpp) | 150 | Execution loop |
| [src/havel-lang/runtime/execution/ExecutionEngine.cpp](src/havel-lang/runtime/execution/ExecutionEngine.cpp) | 300+ | Execution loop impl |

