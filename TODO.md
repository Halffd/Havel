# Ticket: Re-architect Havel's Bytecode Compiler, Optimization Pipeline, and JIT/AOT Backends

## Objective

Build a clean, scalable compiler architecture for Havel that separates:

1. Source-language semantics
2. Semantic/type analysis
3. Bytecode/SSA-like intermediate representation
4. Optimization passes
5. Bytecode/interpreter lowering
6. JIT compilation
7. AOT compilation
8. Runtime/VM semantics
9. Incremental compilation and caching

The long-term goal is to make Havel capable of:

* Fast interpreter execution
* Fast JIT startup
* High-quality optimized JIT code
* Optional LLVM AOT compilation
* Incremental compilation
* Parallel compilation
* Tiered execution
* Shared semantics between VM/JIT/AOT
* Clean backend replacement without rewriting the compiler

The existing `BytecodeOrcJIT.cpp` should **not** be treated as the architectural center of this work. It is currently a large legacy/experimental backend and should eventually become a thin backend implementation over the new IR.

---

# 1. Current Architecture / Problems

Havel currently has several partially overlapping execution/compiler systems.

Important areas include:

```text
src/havel-lang/compiler/
├── BytecodeOrcJIT.h
├── BytecodeOrcJIT.cpp
│
├── core/
│   ├── BytecodeIR.hpp
│   ├── BytecodePasses.hpp
│   └── BytecodePasses.cpp
│
├── vm/
│   └── VMDispatch.cpp
│
├── semantic/
│   └── TypeChecker.cpp
│
└── ...
```

The major architectural problem is that `BytecodeOrcJIT.cpp` has grown into a large monolithic execution backend.

It currently contains substantial amounts of logic that overlap with VM/runtime semantics, JIT compilation, optimization, tiering, profiling, and runtime integration.

The file is approximately 5,500 lines and should eventually be decomposed.

The immediate response should **not** be to split the file arbitrarily.

The correct approach is to establish clear ownership boundaries first.

---

# 2. Architectural Principle

The fundamental rule for the new architecture is:

> **The IR defines compiler-level semantics. Backends consume the IR. The VM/JIT must not independently redefine language semantics.**

Target architecture:

```text
                         Havel Source
                              │
                              ▼
                     Parser / AST
                              │
                              ▼
                     Semantic Analysis
                              │
                              ▼
                     Typed Havel IR
                              │
                              ▼
                        BytecodeIR
                              │
                 ┌────────────┴────────────┐
                 │                         │
                 ▼                         ▼
          Optimization Pipeline       Unoptimized IR
                 │                         │
                 └────────────┬────────────┘
                              │
                    Backend Abstraction
                              │
             ┌────────────────┼────────────────┐
             │                │                │
             ▼                ▼                ▼
        Interpreter       Cranelift JIT     LLVM Backend
             │                │                │
             ▼                ▼                ▼
          VM Runtime       Native Code       Native Code
```

Long term:

```text
                        BytecodeIR
                            │
             ┌──────────────┼──────────────┐
             │              │              │
             ▼              ▼              ▼
          VM Lowering   Cranelift      LLVM ORC/AOT
```

No backend should become the owner of Havel language semantics.

---

# 3. Phase 0 — Establish Baseline

Before further architectural changes, establish a reliable baseline.

## Requirements

Record:

* Current build configurations
* LLVM-enabled build
* LLVM-disabled build
* Self-hosted build
* Non-self-hosted build
* Existing integration tests
* Existing smoke tests
* Existing benchmark results
* Current interpreter performance
* Current startup time
* Current JIT compilation time
* Current memory usage

Run the complete integration suite and classify failures.

Do NOT blindly classify all failures as regressions.

Separate:

```text
PRE_EXISTING
REGRESSION
NEW_FAILURE
INFRASTRUCTURE_FAILURE
TIMEOUT
EXPECTED_FAILURE
```

In particular, distinguish:

```text
exit=127
```

from:

```text
timeout
```

because they likely represent fundamentally different problems.

Establish a machine-readable baseline such as:

```text
tests/baseline/
    default.json
    llvm.json
    self_hosted.json
```

Every subsequent architectural change should be compared against this baseline.

---

# 4. Phase 1 — Stabilize BytecodeIR

`BytecodeIR` becomes the central compiler representation.

It must remain independent from:

* LLVM
* ORC
* Cranelift
* VM implementation details
* OS-specific runtime code

## Core IR

The IR should support:

```text
Module
 └── Function
      ├── locals
      ├── parameters
      ├── constants
      └── BasicBlocks
           ├── instructions
           └── terminator
```

Each function should have:

```text
BytecodeFunction
```

with:

* parameters
* locals
* return type
* blocks
* metadata
* source locations
* optimization metadata

---

# 5. Basic Blocks and CFG

Every function should eventually be represented as an explicit control-flow graph.

```text
Block 0
   │
   ├── condition ──────► Block 2
   │
   ▼
Block 1
   │
   ▼
Block 3
```

Terminators should explicitly represent:

```text
Jump
Branch
Return
Throw
Unreachable
Switch
```

Avoid encoding control flow implicitly in instruction sequences.

The CFG must be independently inspectable and verifiable.

---

# 6. Typed Locals and Places

Continue the `Place` abstraction.

The IR should distinguish:

```text
Local
Place
PlaceElement
```

where appropriate.

Example conceptual representation:

```text
local %0 : Int
local %1 : String

%0 = const 10
%1 = load ...
```

The exact syntax is implementation-defined; the important requirement is that the compiler has enough type information to reason about values without repeatedly reconstructing it from VM state.

---

# 7. FunctionBuilder

Keep the `FunctionBuilder` / `BlockAnd` style API.

The builder should provide structured operations such as:

```text
create_block()
switch_to_block()
emit()
terminate()
jump()
branch()
return_value()
```

The builder must prevent malformed CFGs where practical.

The goal is to make invalid IR difficult to construct.

---

# 8. IR Validation

Introduce a dedicated IR verifier.

Validation should check:

* block IDs
* predecessor consistency
* successor consistency
* valid terminators
* valid local references
* valid operand references
* type consistency
* dominance requirements where applicable
* unreachable block rules
* malformed control flow
* invalid place references

Example:

```text
verify_function(func)
verify_module(module)
```

Validation should be cheap enough to run during development.

Debug builds should validate after optimization passes.

Release builds may selectively disable expensive validation.

---

# 9. Phase 2 — Optimization Pass Framework

The pass framework should become a real compiler subsystem rather than a collection of placeholder classes.

Target:

```text
PassManager
    │
    ├── SimplifyCFG
    ├── ConstantPropagation
    ├── DCE
    ├── CopyPropagation
    ├── TypePropagation
    ├── Inlining
    ├── LICM
    └── Validation
```

Every pass must declare:

```text
name
dependencies
preserved analyses
required analyses
modified state
```

Do not rely only on comments to describe dependencies.

---

# 10. Pass Pipeline

Initial standard pipeline:

```text
Validate
   ↓
SimplifyCFG
   ↓
Validate
   ↓
ConstantPropagation
   ↓
Validate
   ↓
DeadCodeElimination
   ↓
Validate
   ↓
CopyPropagation
   ↓
Validate
   ↓
Inlining
   ↓
Validate
```

Later:

```text
SimplifyCFG
ConstantPropagation
TypePropagation
SCCP
DCE
CopyPropagation
Inlining
LICM
StrengthReduction
Lowering
```

---

# 11. Constant Propagation

The initial implementation must go beyond a placeholder.

It should understand:

```text
constant local
constant arithmetic
constant comparisons
constant branches
```

Example:

```text
a = 10
b = 20
c = a + b
```

should become conceptually:

```text
c = 30
```

This should feed subsequent CFG simplification and DCE.

---

# 12. Dead Code Elimination

DCE must eventually operate on dataflow/use-def information rather than only local instruction patterns.

It should eliminate:

* unreachable blocks
* unused pure values
* redundant stack operations
* dead calculations
* dead branches

It must preserve instructions with side effects.

Never classify an instruction as removable merely because its result is unused without knowing its side-effect semantics.

---

# 13. Side-Effect Model

Introduce explicit instruction properties:

```text
Pure
ReadOnly
MayThrow
HasSideEffects
Terminates
Allocates
Calls
```

This becomes critical for correct optimization.

For example:

```text
LOAD_CONST 42
POP
```

can be eliminated.

But:

```text
call foo()
POP
```

cannot be eliminated merely because the return value is unused.

---

# 14. Inlining

Inlining should initially be conservative.

Implement:

* function size limits
* recursion detection
* call frequency heuristics
* hot/cold information
* maximum expansion budget

Do not implement aggressive global inlining until profiling exists.

Target:

```text
small + hot → inline
large + cold → don't inline
recursive → don't inline
```

---

# 15. Phase 3 — Fast Opcode Architecture

The existing fast integer operations should remain.

Examples:

```text
ADD_INT
SUB_INT
MUL_INT
DIV_INT
MOD_INT
```

The compiler should emit these when type information proves operands are integers.

The optimization system should understand them as normal IR operations rather than treating them as special VM hacks.

Conceptually:

```text
Generic ADD
     │
     │ type proven
     ▼
Integer Add
     │
     ▼
ADD_INT VM lowering
```

This allows the same operation to eventually lower to:

```text
VM → ADD_INT
Cranelift → native integer add
LLVM → native integer add
```

---

# 16. Native String Cursor

Keep the native UTF-8 cursor architecture.

Current operations:

```text
STRING_CURSOR_NEW
STRING_CURSOR_CURRENT
STRING_CURSOR_ADVANCE
STRING_CURSOR_PEEK
STRING_CURSOR_RESET
STRING_CURSOR_GET_POS
STRING_CURSOR_SET_POS
```

The cursor should be treated as a runtime primitive with well-defined semantics.

The IR should not contain arbitrary implementation-specific pointer manipulation.

Long-term:

```text
StringCursor operation
       │
       ├── VM runtime implementation
       ├── Cranelift lowering
       └── LLVM lowering
```

---

# 17. Phase 4 — Runtime Semantics Separation

Create a clear boundary between compiler IR and VM runtime.

Suggested conceptual layers:

```text
compiler/
    core/
        BytecodeIR
        BytecodeBuilder
        BytecodeVerifier
        BytecodePasses

    analysis/
        TypeAnalysis
        CFGAnalysis
        Liveness
        CallGraph

    backend/
        Backend.hpp
        VMBackend
        CraneliftBackend
        LLVMBackend

runtime/
    Value
    Objects
    GC
    Strings
    IO
    Exceptions
```

The exact directory names can differ, but ownership must be explicit.

---

# 18. Backend Interface

Introduce a backend abstraction.

Conceptually:

```text
class CompilerBackend {
public:
    virtual compile(function) = 0;
    virtual compile_module(module) = 0;
    virtual lookup(symbol) = 0;
};
```

The exact API should be designed around actual Havel requirements.

Backends should consume validated IR.

They should not need to parse Havel AST.

---

# 19. VM Backend

The interpreter becomes the simplest backend.

```text
BytecodeIR
    ↓
VM bytecode
    ↓
VMDispatch
```

The VM remains the baseline execution engine.

This is important because it gives every JIT backend a known-correct reference implementation.

Whenever possible:

```text
VM result == JIT result == AOT result
```

for identical inputs.

---

# 20. BytecodeOrcJIT Refactor

## IMPORTANT

Do NOT begin by manually splitting `BytecodeOrcJIT.cpp` into 20 files.

First establish the new IR/backend boundaries.

Then migrate functionality into those boundaries.

The current ORC implementation should be treated as a legacy backend.

Its responsibilities should eventually become:

```text
BytecodeOrcJIT
    │
    ├── LLVM IR lowering
    ├── ORC session management
    ├── symbol resolution
    ├── object loading
    └── compiled-code lookup
```

It should NOT own:

```text
Havel semantics
VM semantics
type inference
CFG construction
general optimization
GC semantics
string semantics
parser logic
language-level exception semantics
```

---

# 21. BytecodeOrcJIT Decomposition

Eventually split the monolith conceptually into:

```text
LLVMBackend
    │
    ├── LLVMContextManager
    ├── LLVMModuleBuilder
    ├── LLVMIRLowering
    ├── LLVMRuntimeABI
    ├── LLVMOptimization
    └── LLVMCodeCache

ORC/
    ├── OrcJITSession
    ├── OrcSymbolResolver
    ├── OrcCompileLayer
    ├── OrcObjectLayer
    └── OrcMemoryManager
```

Profiling/tiering should be separate:

```text
execution/
    Profiler
    HotCounter
    TierManager
```

Compilation queue:

```text
jit/
    CompilationQueue
    CompilationTask
    CompilationWorker
```

Do not allow all of these concerns to remain in one translation unit.

---

# 22. LLVM JIT vs LLVM AOT

Treat LLVM as two related but distinct use cases.

## JIT

Optimize for:

```text
low compilation latency
lazy compilation
hot function compilation
runtime symbol resolution
tiering
```

Pipeline should favor fast compilation.

## AOT

Optimize for:

```text
maximum code quality
whole-program optimization
LTO
cross-module optimization
dead stripping
binary size
```

Pipeline may use:

```text
O2/O3
ThinLTO
LTO
```

depending on build mode.

Do not force the JIT and AOT paths to use identical optimization pipelines.

---

# 23. LLVM Context / Module Architecture

Avoid unnecessary LLVM context creation.

Potential architecture:

```text
LLVMCompiler
    │
    ├── shared context
    ├── module pool
    ├── target machine
    └── pass manager
```

For parallel compilation, carefully determine which LLVM objects can safely be shared and which require independent contexts/modules.

Do not assume "single LLVMContext" automatically means better scalability.

Benchmark it.

---

# 24. Cranelift Backend

Add Cranelift as the preferred low-latency JIT candidate.

Target architecture:

```text
BytecodeIR
    ↓
CraneliftLowering
    ↓
Cranelift IR
    ↓
native code
```

Cranelift should share:

* runtime ABI
* function metadata
* symbol resolution
* profiling
* hotness information

with the LLVM backend.

It should not duplicate compiler semantics.

---

# 25. Tiered Execution

Long-term execution architecture:

```text
Tier 0
Interpreter
   │
   │ function becomes hot
   ▼
Tier 1
Fast JIT / Cranelift
   │
   │ very hot
   ▼
Tier 2
LLVM optimized JIT
```

Possible alternative:

```text
Interpreter
   ↓
Cranelift
   ↓
LLVM
```

The tier manager decides based on:

* invocation count
* loop count
* execution time
* compilation cost
* function size

Avoid implementing tiering until profiling data exists.

---

# 26. Profiling Infrastructure

Introduce lightweight runtime profiling.

Track:

```text
function invocation count
loop iteration count
call-site frequency
exception frequency
allocation frequency
execution time
```

The profiler should have low overhead.

It should feed:

```text
Inlining
JIT tier selection
Hot/cold partitioning
Compilation priority
```

Do not add complex PGO infrastructure before the basic counters are trustworthy.

---

# 27. Runtime ABI

Define a stable internal runtime ABI for generated code.

Examples:

```text
allocate
GC barriers
string operations
exception throwing
function calls
dynamic dispatch
property access
FFI calls
```

Generated native code should call runtime functions through this ABI instead of embedding VM implementation details.

This is one of the most important prerequisites for making LLVM and Cranelift backends maintainable.

---

# 28. Garbage Collector Boundary

Generated code must interact with GC through explicit runtime interfaces.

Do not duplicate GC algorithms in each backend.

Conceptually:

```text
native code
    │
    ▼
Runtime ABI
    │
    ▼
GC
```

Both LLVM and Cranelift must use the same ownership/barrier rules.

---

# 29. Exception Semantics

Define language-level exception behavior independently from backend implementation.

The IR should represent:

```text
throw
catch
exception edge
unwind
```

Backends lower these semantics into their respective mechanisms.

The LLVM backend should not become the definition of Havel exceptions.

---

# 30. FFI

Keep FFI at the runtime boundary.

The compiler may know:

```text
function signature
```

but should not embed arbitrary FFI implementation details into optimization passes.

Generated code should ultimately call a well-defined runtime FFI layer.

---

# 31. Incremental Compilation

After IR and backend boundaries stabilize, implement query-driven incremental compilation.

Conceptual architecture:

```text
Source
  ↓
Fingerprint
  ↓
Query
  ↓
Dependency graph
  ↓
Cached IR / bytecode
```

Cache layers:

```text
Memory cache
     ↓
Disk cache
     ↓
Recompile
```

Possible artifacts:

```text
.hvb  → compiled bytecode / IR
.hvd  → dependency metadata
.hvs  → optional optimization/profile summaries
```

Cache keys should include:

```text
source hash
dependency hashes
compiler version
IR version
optimization level
target architecture
relevant configuration
```

Never reuse incompatible cached native code.

---

# 32. Red/Green Incremental Model

Adopt a simplified red/green model.

```text
GREEN
source + dependencies unchanged
→ reuse cached artifact

RED
dependency changed
→ recompute affected queries
```

Track dependency edges automatically during module compilation.

Do not attempt to reproduce all of rustc's query system.

Implement only what Havel needs.

---

# 33. Parallel Compilation

Once incremental compilation exists, allow independent modules/functions to compile concurrently.

Potential architecture:

```text
CompilationScheduler
       │
       ├── Task A
       ├── Task B
       ├── Task C
       └── Task D
```

Tasks should synchronize only when dependencies require it.

For JIT:

```text
hot function
    ↓
CompilationQueue
    ↓
worker
    ↓
backend
```

The interpreter should continue executing while compilation occurs.

---

# 34. Bitcode / IR Caching

LLVM AOT should support caching where beneficial.

Potential flow:

```text
Havel source
    ↓
BytecodeIR
    ↓
optimized IR
    ↓
LLVM bitcode
    ↓
cache
```

On unchanged input:

```text
cache hit
   ↓
skip frontend/codegen work
```

Do not prematurely cache everything.

Measure compilation time first and cache only expensive stages.

---

# 35. Testing Strategy

Testing must exist at multiple levels.

## Unit Tests

Test:

```text
IR construction
CFG
Place
Builder
Verifier
individual passes
type propagation
constant propagation
DCE
lowering
runtime ABI
```

## Differential Tests

Run identical programs through:

```text
VM
Cranelift
LLVM JIT
AOT
```

and compare:

```text
stdout
stderr
exit code
exceptions
observable state
```

The VM becomes the reference implementation.

## Fuzzing

Eventually fuzz:

```text
parser → IR
IR verifier
optimization passes
VM execution
JIT execution
```

Every optimization pass should preserve behavior.

---

# 36. Optimization Correctness

For every optimization:

```text
original IR
    ↓
optimized IR
```

must satisfy:

```text
observable(original) == observable(optimized)
```

except where explicitly permitted by language semantics.

Add regression tests for every optimizer bug.

Optimization must never be justified by:

> "It seems faster."

Correctness comes first.

---

# 37. Benchmark Suite

Create dedicated benchmarks.

Categories:

```text
integer arithmetic
string processing
UTF-8 scanning
arrays
objects
function calls
closures
loops
exceptions
FFI
I/O
allocation
GC
modules
```

Measure:

```text
startup time
compile time
JIT compile latency
execution time
memory
code size
```

Track:

```text
interpreter
fast interpreter
Cranelift
LLVM JIT
AOT
```

---

# 38. Performance Targets

Do not hard-code arbitrary speedup promises.

Establish baseline first.

Then define targets relative to baseline.

Example:

```text
Interpreter:
    baseline = 1.0×

Fast opcodes:
    target = measurable improvement

Cranelift:
    target = significantly lower compile latency than LLVM

LLVM:
    target = higher peak throughput for hot workloads
```

Compilation latency is as important as peak execution speed for a scripting language.

---

# 39. Build Configuration

Keep development builds fast.

Recommended conceptual configurations:

```text
Debug
    LLVM JIT disabled
    maximum diagnostics

Debug + LLVM
    LLVM enabled
    JIT tests enabled

Release
    optimized VM

Release + JIT
    Cranelift/LLVM enabled

AOT
    full optimization
    LTO where appropriate
```

The default developer build should not require expensive LLVM compilation unless needed.

---

# 40. Migration Strategy for BytecodeOrcJIT

Migration must be incremental.

## Stage A

Keep existing `BytecodeOrcJIT`.

Introduce:

```text
BytecodeIR
Backend interface
Runtime ABI
```

No behavioral change required.

## Stage B

Make ORC consume the new IR for one simple function category.

For example:

```text
integer arithmetic
```

Compare:

```text
old ORC
new backend
VM
```

## Stage C

Move more operations:

```text
locals
calls
branches
loops
strings
objects
exceptions
```

## Stage D

Move profiling/tiering out of ORC.

## Stage E

Move runtime calls behind the ABI.

## Stage F

Remove duplicated VM semantics.

## Stage G

Delete obsolete ORC implementation.

The final ORC backend should be substantially smaller than the current monolith.

---

# 41. What NOT to Do

Do not:

* Rewrite all of `BytecodeOrcJIT.cpp` at once
* Introduce LLVM dependencies into `BytecodeIR`
* Make LLVM IR the primary Havel IR
* Make Cranelift IR the primary Havel IR
* Duplicate runtime semantics per backend
* Implement aggressive optimization before correctness infrastructure
* Add tiering before profiling exists
* Build an enormous rustc-style query system
* Add caching before establishing cache invalidation rules
* Claim performance improvements without benchmarks
* Treat every current test failure as caused by the refactor
* Hide compiler errors behind generic runtime errors

---

# 42. Recommended Priority

The implementation order should be:

```text
1. Baseline tests
        ↓
2. BytecodeIR stabilization
        ↓
3. IR verifier
        ↓
4. FunctionBuilder
        ↓
5. CFG analysis
        ↓
6. Pass infrastructure
        ↓
7. SimplifyCFG
        ↓
8. Constant propagation
        ↓
9. DCE
        ↓
10. Copy/type propagation
        ↓
11. Fast opcode lowering
        ↓
12. Runtime ABI
        ↓
13. VM backend cleanup
        ↓
14. Backend abstraction
        ↓
15. Cranelift prototype
        ↓
16. LLVM lowering prototype
        ↓
17. BytecodeOrcJIT migration
        ↓
18. Profiling
        ↓
19. Tiered JIT
        ↓
20. Incremental compilation
        ↓
21. Parallel compilation
        ↓
22. LLVM AOT/LTO
```

---

# 43. Definition of Done

The architecture is considered successful when:

### Compiler

* BytecodeIR is the canonical compiler representation.
* CFG is explicit.
* IR has a verifier.
* Passes have explicit dependencies.
* Optimizations are independently testable.
* Fast operations are represented cleanly.

### VM

* VM remains a correct reference backend.
* VM semantics are not duplicated in JIT code.
* Fast integer operations work.
* Native string cursor works.
* Runtime/GC interfaces are centralized.

### JIT

* Backend interface exists.
* Cranelift can compile validated IR.
* LLVM can compile validated IR.
* Both use the same runtime ABI.
* Generated code does not redefine language semantics.

### LLVM

* JIT and AOT pipelines are separately configurable.
* LLVM optimization is no longer intertwined with VM logic.
* ORC session management is isolated.
* LLVM lowering is independently testable.

### ORC

`BytecodeOrcJIT.cpp` is either:

* significantly reduced, or
* replaced entirely,

with its remaining code focused specifically on LLVM/ORC backend responsibilities.

### Incremental compilation

* Source fingerprints work.
* Dependency invalidation works.
* Cached artifacts are versioned.
* Unchanged modules avoid unnecessary recompilation.

### Testing

* VM/JIT/AOT differential tests exist.
* Optimizer regression tests exist.
* Benchmarks exist.
* Current failures have known classifications.
* No optimization is merged solely on the basis of a theoretical speedup.

---

# 44. Final Architecture

The desired end state is:

```text
                         ┌───────────────┐
                         │   Havel AST   │
                         └───────┬───────┘
                                 │
                                 ▼
                         ┌───────────────┐
                         │ Semantic/Type │
                         │    Analysis   │
                         └───────┬───────┘
                                 │
                                 ▼
                     ┌──────────────────────┐
                     │      BytecodeIR      │
                     │                      │
                     │ CFG                  │
                     │ BasicBlocks          │
                     │ Typed Locals         │
                     │ Places               │
                     │ Terminators          │
                     └──────────┬───────────┘
                                │
                                ▼
                     ┌──────────────────────┐
                     │   Pass Manager       │
                     │                      │
                     │ SimplifyCFG           │
                     │ ConstProp             │
                     │ DCE                   │
                     │ CopyProp              │
                     │ Inlining              │
                     │ LICM                  │
                     │ Validation            │
                     └──────────┬───────────┘
                                │
                    ┌───────────┴───────────┐
                    │                       │
                    ▼                       ▼
             ┌─────────────┐        ┌──────────────┐
             │ VM Backend  │        │ JIT Backends │
             └──────┬──────┘        └──────┬───────┘
                    │                      │
                    ▼                 ┌────┴─────┐
             ┌─────────────┐           │          │
             │ VM Runtime  │      Cranelift     LLVM
             └──────┬──────┘           │          │
                    │                  └────┬─────┘
                    │                       │
                    └──────────┬────────────┘
                               ▼
                       ┌──────────────┐
                       │ Runtime ABI  │
                       ├──────────────┤
                       │ GC           │
                       │ Strings      │
                       │ Exceptions   │
                       │ FFI          │
                       │ Allocation   │
                       └──────────────┘
```

And above the compilation system:

```text
              Incremental Query / Cache System
                           │
              ┌────────────┴────────────┐
              │                         │
          Module cache             Function cache
              │                         │
              └────────────┬────────────┘
                           │
                    Compilation graph
                           │
                    Parallel scheduler
```

The fundamental architectural rule remains:

> **One language semantics, one canonical IR, one runtime ABI, multiple backends.**

LLVM should eventually be a backend.

Cranelift should eventually be a backend.

The VM should be a backend/reference implementation.

`BytecodeOrcJIT.cpp` should **not** be the place where Havel's compiler architecture lives.

The immediate goal is therefore **not "make LLVM JIT faster."**

The immediate goal is to build the abstraction that makes *all* execution strategies independently replaceable, testable, and optimizable. Once that exists, making ORC fast—or replacing it entirely—becomes a backend engineering problem instead of another compiler rewrite.

Yeah — **if “I just want speed” is the actual objective, this synthesis points in the right direction, but I would change the priority order substantially.**

The big mistake would be jumping straight into the 8–10 week roadmap. Havel already has a working VM/compiler, so the first goal should be to identify **where the runtime is actually burning time** and attack that layer.

### What I'd do instead

**Phase 0 — establish a baseline**

Before LLVM, Cranelift, ORC, LTO, etc.:

```text
source
 ↓
load modules
 ↓
self-hosted lexer
 ↓
parser
 ↓
typecheck
 ↓
bytecode emission
 ↓
VM execution
 ↓
host calls
```

Measure each independently.

Your recent lexer experiment already demonstrated why this matters:

```text
cpArr lookup                  cheap
       ↓
string._cpAt() per character expensive
```

A native function call per character is exactly the sort of optimization that looks beautiful in C++ and murders an interpreter.

---

## Then optimize Havel in this order

### 1. Make the current bytecode VM fast

This is probably your highest ROI.

Things like:

* direct opcode dispatch
* specialized integer operations
* efficient locals
* avoiding unnecessary `Value` construction/copies
* reducing heap allocations
* inline/cache property lookups
* faster function calls
* faster array indexing
* reducing host-function dispatch overhead

If the VM executes:

```text
1 billion VM instructions/sec
```

versus:

```text
50 million VM instructions/sec
```

it doesn't matter how fancy the eventual JIT architecture is. Your baseline is already dramatically different.

---

### 2. Build an actual IR/CFG

This part of your Rust research is **very applicable**.

Instead of optimizing:

```text
flat bytecode
```

introduce:

```text
Function
 ├── Block 0
 │    ├── instruction
 │    ├── instruction
 │    └── branch → Block 2
 │
 ├── Block 1
 │    └── ...
 │
 └── Block 2
      └── return
```

Then you can do:

```text
CFG simplify
 ↓
constant propagation
 ↓
DCE
 ↓
copy propagation
 ↓
inlining
```

And importantly, **validate the IR after every transformation**.

That gives you an optimization foundation regardless of whether the eventual backend is LLVM, Cranelift, or a custom JIT.

---

### 3. Add a JIT only after the IR is good

This is where your Cranelift research becomes useful.

I'd strongly favor:

```text
Havel IR
   ↓
Cranelift
   ↓
native machine code
```

over immediately designing:

```text
Havel IR
   ↓
LLVM IR
   ↓
ORC
   ↓
LLVM optimization pipeline
   ↓
machine code
```

for the first implementation.

Cranelift gives you a much faster compile-time path, which matters for a language that wants **JIT responsiveness**.

The architecture could eventually be:

```text
                 Havel source
                      │
             lexer / parser
                      │
                 Havel IR
                      │
             ┌────────┴────────┐
             │                 │
          Bytecode          Cranelift
             │                 │
             ▼                 ▼
            VM              native code
```

Same IR, two execution modes.

That's much more valuable than embedding C.

---

## 4. Make host calls cheap

This is actually **very important for Havel specifically**.

You currently have a lot of functionality exposed through:

```text
Havel
 ↓
host function lookup
 ↓
Value vector
 ↓
C++
 ↓
Value
```

That's fine for:

```havel
fs.readFile(...)
```

because you're doing substantial work per call.

It's terrible for:

```havel
string._cpAt(...)
string._cpAt(...)
string._cpAt(...)
string._cpAt(...)
...
```

So I'd establish a rule:

> **Native calls must do enough work to amortize their dispatch cost.**

For example, this is good:

```havel
string._decodeAll(source)
```

if it processes 100,000 characters.

This is bad:

```havel
for c in source:
    string._decodeOne(c)
```

if `_decodeOne` crosses the VM/native boundary every iteration.

---

# The really interesting part: one IR, multiple execution tiers

Eventually Havel could have something like:

```text
                    Havel IR
                       │
          ┌────────────┼────────────┐
          │            │            │
          ▼            ▼            ▼
       Bytecode      Cranelift    LLVM/AOT
          │            │            │
          ▼            ▼            ▼
       interpreter    JIT        executable
```

And then execution could be tiered:

```text
function called once
       ↓
   bytecode VM

function called 10 times
       ↓
   maybe compile

function called 10,000 times
       ↓
   optimized native code
```

That's where you get the **actual speed story**.

The interpreter becomes the cheap startup path.

The JIT becomes the hot-path accelerator.

AOT becomes the deployment/maximum-optimization path.

---

## I'd also postpone LTO

Your synthesis has:

> Phase 1: AOT + LLVM Foundation

I'd actually reverse that.

Don't make LLVM/AOT foundational to the whole compiler yet.

Make this foundational:

```text
Havel AST
   ↓
typed Havel IR / CFG
   ↓
optimization passes
   ↓
backend interface
```

Then:

```text
Backend::Bytecode
Backend::Cranelift
Backend::LLVM
```

LLVM can arrive later.

Otherwise you risk spending weeks building an extremely sophisticated native backend while the actual bottleneck is something stupid like:

```text
Value allocation
property lookup
closure invocation
host dispatch
```

Classic compiler engineering: **don't optimize the cathedral before measuring the loose brick.**

### So my priority would be

```text
0. Benchmark/profiling
        ↓
1. Optimize existing VM
        ↓
2. CFG-based Havel IR
        ↓
3. IR optimization
        ↓
4. Cranelift JIT
        ↓
5. Tiered execution
        ↓
6. Incremental compilation/cache
        ↓
7. LLVM/AOT/LTO
```

And **inline C isn't on the list** unless you specifically want users to write native extensions more conveniently.

For raw Havel performance, the winning move is **a good IR + fast VM + JIT**, not putting C syntax into `.hv` files. —



Yeah — and I’d sharpen that recommendation quite a bit.

You effectively have **two different LLVM layers**, and they shouldn't be treated as one project:

1. **`BytecodeIR`** → compiler-side intermediate representation. **High priority.**

   * This is where the CFG/basic blocks, typed locals, `Place`, `FunctionBuilder`, optimization passes, etc. belong.
   * It should become the clean abstraction between Havel source/bytecode generation and whatever backend eventually executes it.
   * This work directly supports optimization even if you never use LLVM.

2. **`BytecodeOrcJIT`** → execution backend. **Low priority for now.**

   * 5,544-line monolith.
   * Duplicates VM semantics.
   * Has its own JIT machinery/tiering/optimization infrastructure.
   * LLVM JIT is disabled in your normal debug build.
   * It has comparatively little meaningful coverage.
   * Most importantly: **it doesn't solve the immediate bottleneck you're working on.**

### The architectural direction I'd use

```text
Havel source
    │
    ▼
AST / semantic analysis
    │
    ▼
BytecodeIR                 ← focus here
  ├─ BasicBlock
  ├─ Terminator
  ├─ Local / Place
  ├─ typed operations
  └─ optimization passes
    │
    ├───────────────┐
    ▼               ▼
Interpreter VM      JIT backend
  │                 │
  │                 ├─ Cranelift
  │                 └─ LLVM ORC (later)
  │
  ▼
fast opcodes
```

The key is **don't make `BytecodeOrcJIT` define the semantics of Havel**.

The VM and IR should define semantics; JITs should translate those semantics.

### So I'd explicitly defer the 5,544-line monster

Not delete it. Not rewrite it yet.

Mark it as something like:

> **Legacy/experimental execution backend — do not expand until BytecodeIR is stable.**

Then once `BytecodeIR` is mature, the JIT can consume IR:

```text
BytecodeIR
    │
    ├── VM lowering
    │
    ├── Cranelift lowering
    │
    └── LLVM lowering
```

That completely changes the economics of the LLVM JIT.

Instead of:

> "Let's make BytecodeOrcJIT understand everything the VM understands."

you get:

> "Let's write an IR → LLVM lowering."

Much smaller problem.

And **that's also where your Rust research becomes useful**. The useful part isn't "copy rustc's LLVM architecture." It's the separation:

```text
frontend
   ↓
IR
   ↓
optimization
   ↓
backend
```

### One important correction to the previous agent's conclusion

I wouldn't say:

> "Cranelift is 10× faster than LLVM, therefore use Cranelift."

That's too simplistic.

For Havel, the more important distinction is:

**JIT compilation latency vs generated-code quality.**

For a scripting language:

```text
short script:
  compile time matters enormously
  └── Cranelift is attractive

long-running hot function:
  generated code matters more
  └── LLVM becomes attractive
```

So eventually having **both** is actually reasonable:

```text
Tier 0 → interpreter
       ↓ hot
Tier 1 → Cranelift JIT
       ↓ very hot
Tier 2 → LLVM ORC / optimized AOT
```

But that's *later*.

### And your current test output reinforces this

Those integration results you showed earlier are a much bigger priority than polishing ORC:

```text
7-ish tests → timeout
many tests → exit=127
some → PASS
```

Especially because you have evidence that the **non-LLVM/self-hosted path is doing real work**, while LLVM JIT isn't even enabled in the normal development configuration.

I'd therefore prioritize:

**1. Fix the `exit=127` infrastructure problem**
**2. Determine the timeout cluster/root cause**
**3. Stabilize BytecodeIR**
**4. Make the optimization passes actually correct/useful**
**5. Benchmark interpreter + fast opcodes**
**6. Integrate the new IR into the real compiler path**
**7. Then design a clean JIT backend interface**
**8. Only then revisit `BytecodeOrcJIT`**

And when #8 arrives, I'd strongly favor **incremental extraction/replacement**, not trying to "clean up" all 5,544 lines in place.

In other words: **BytecodeIR is infrastructure. BytecodeOrcJIT is currently legacy debt. Don't let the debt dictate the architecture.** —


# Rust Query System Insights for Havel Build System

## 1. Query-Based Demand-Driven Compilation Model

### Core Architecture
- **Queries as pure functions**: Each query takes a key (e.g., `DefId`) and returns a value. Results are cached.
- **On-demand execution**: Queries only run when needed by downstream consumers.
- **Implicit dependency tracking**: Reading a query result automatically records a dependency edge in the dep-graph.
- **Parallel execution**: Multiple queries run concurrently using Rayon thread pool with latch-based synchronization.

### Key Components
```
QueryConfig          - Defines query metadata (name, dep_kind, hash_result, etc.)
QueryContext         - Trait implemented by TyCtxt providing execution environment
QueryState<K, I>     - Sharded hashmap tracking active/pending queries
QueryCache           - Trait for in-memory caches (DefaultCache, DefIdCache, SingleCache, VecCache)
QueryJob             - Represents an executing query with latch for waiters
```

### Query Execution Flow
1. **Check in-memory cache** → hit → return cached value + record dep-graph read
2. **Check active query map** → already running → wait on latch (parallel) or cycle error (sync)
3. **Start new job** → insert into active map → execute provider function
4. **On completion** → store in cache → signal latch → remove from active map
5. **On panic** → poison query → waiters receive fatal error

## 2. Incremental Compilation Dependency Tracking

### Dependency Graph Structure
- **DepNode**: `(DepKind, Fingerprint)` - uniquely identifies a query invocation
- **DepNodeIndex**: Compact integer index for current session nodes
- **SerializedDepNodeIndex**: Index into previous session's graph (for cross-session references)
- **Edges**: Stored as `Vec<DepNodeIndex>` per node (inline capacity for small edge counts)

### Red/Green Marking Algorithm
```
try_mark_green(node):
  if node not in previous graph → return None (new node, must compute)
  if node already Green → return index
  if node already Red → return None

  // Unknown state: recursively mark all dependencies green
  for each dep in previous_graph.edges(node):
    if !try_mark_green(dep): return None

  // All deps green → promote node to current graph
  promote_node_and_deps_to_current(prev_index)
  colors[prev_index] = Green(new_index)
  return Some(new_index)
```

### Key Insight: Dependency Tracking via TLS
- **TaskDeps** stored in thread-local storage during query execution
- `read_index(dep_node_index)` called automatically when query accesses another query's result
- `with_deps(TaskDepsRef::Allow(&task_deps), || task(...))` wraps query execution
- No explicit dependency declarations needed - tracking is implicit via query calls

## 3. Caching and Invalidation Strategies

### Three-Tier Caching
```
┌─────────────────────────────────────────────────────┐
│              In-Memory Query Cache                  │
│  (DefaultCache / DefIdCache / SingleCache / VecCache)│
│  - Fast lookup by key                               │
│  - Stores (value, DepNodeIndex)                     │
│  - Per-session only                                 │
└─────────────────────┬───────────────────────────────┘
                      │ cache miss
                      ▼
┌─────────────────────────────────────────────────────┐
│            On-Disk Cache (query cache file)         │
│  - Serialized query results                         │
│  - Loaded on-demand when node marked green          │
│  - Verified via fingerprint on load                 │
└─────────────────────┬───────────────────────────────┘
                      │ cache miss / fingerprint mismatch
                      ▼
┌─────────────────────────────────────────────────────┐
│              Re-execute Query Provider              │
│  - Full recomputation                               │
│  - Results written to both caches                   │
└─────────────────────────────────────────────────────┘
```

### Invalidation via Fingerprint Comparison
- **Result hashing**: `hash_result(fn(&mut StableHashingContext, &Value) -> Fingerprint)`
- **Green node**: Previous fingerprint == current fingerprint
- **Red node**: Fingerprint differs → must recompute
- **No-hash queries**: Always treated as red (conservative)

### Cache Invalidation Triggers
1. **Source file changes** → different commandline args hash → full cache invalidation
2. **Query result fingerprint mismatch** → node marked red → downstream dependents become red
3. **Missing work products** (object files) → deleted, forcing recompilation
4. **Compiler version mismatch** → cache discarded

## 4. Salsa-like Query System Architecture

### Similarities to Salsa
| Salsa Concept | Rust Query System |
|--------------|-------------------|
| `Query<K, V>` | `QueryConfig` + provider function |
| `Database` | `QueryContext` (implemented by `TyCtxt`) |
| `Memoization` | In-memory `QueryCache` + on-disk cache |
| `Dependency tracking` | Implicit via `read_index()` in TLS |
| `Invalidation` | Red/green marking + fingerprint comparison |
| `Parallel execution` | Rayon + `QueryLatch` (condvar-based) |

### Key Differences from Salsa
1. **Two-phase graph**: Separate "previous" (immutable) and "current" (building) graphs
2. **Explicit dep-node construction**: `DepNode` built from key, not auto-derived
3. **Anonymous queries**: Dep-node ID = hash of dependency set + session seed
4. **Side effects**: Diagnostics recorded as special dep-nodes, replayed on green
5. **Work products**: Separate tracking for artifacts (object files, etc.)

### Cycle Handling
- **Detection**: DFS over active query wait-for graph
- **Breaking**: Deterministic selection of latch-waited edge to resume
- **Error reporting**: Full cycle stack with spans and descriptions
- **Single-threaded**: Immediate cycle error (no latches)

## 5. Techniques Applicable to Havel

### A. Implicit Dependency Tracking
```rust
// Instead of manual dep tracking, wrap computation:
fn compute_query(key) -> Value {
    let task_deps = TaskDeps::new();
    with_deps(TaskDepsRef::Allow(&task_deps), || {
        provider(key)  // Any query calls inside automatically recorded
    })
}
```
**Apply to Havel**: Wrap bytecode compilation phases in tracked tasks. Module imports, type checking, etc. automatically build dependency graph.

### B. Red/Green Incremental Compilation
```rust
// Check if cached result is valid:
fn try_load_cached(key) -> Option<Value> {
    let dep_node = make_dep_node(key);
    if let Some((prev_idx, curr_idx)) = dep_graph.try_mark_green(dep_node) {
        // Load from on-disk cache, verify fingerprint
        return load_from_disk(key, prev_idx, curr_idx);
    }
    None  // Must recompute
}
```
**Apply to Havel**:
- Hash AST + dependencies for each module
- On rebuild, check if module's transitive deps unchanged
- Skip recompilation if green, only re-execute red nodes

### C. Specialized Cache Types
```rust
// Havel can use different caches for different query types:
DefIdCache      → Module-level queries (keyed by ModuleId)
SingleCache     → Global queries (e.g., "all types", "all functions")
VecCache        → Dense integer keys (e.g., function indices in module)
DefaultCache    → General purpose (e.g., type inference results)
```

### D. On-Disk Cache with Fingerprint Verification
```rust
// Save: serialize result + fingerprint
// Load: deserialize → recompute fingerprint → verify match
// Mismatch → discard, recompute
```
**Apply to Havel**: Cache bytecode, type info, symbol tables to disk. Verify on load.

### E. Work Product Tracking
```rust
// Track generated artifacts (object files, bytecode files)
struct WorkProduct {
    module_name: String,
    files: HashMap<String, PathBuf>,  // e.g., "hv" -> bytecode, "dbg" -> debug info
    content_hash: Fingerprint,         // Hash of module's exported symbols
}
```
**Apply to Havel**: Track `.hvb` bytecode files, `.hvd` debug info. Reuse if content hash matches.

### F. Anonymous Queries for Fine-Grained Tracking
```rust
// For queries without stable key (e.g., "all items in module"):
fn anon_query(dep_kind, computation) {
    let deps = track_dependencies(computation);
    let anon_id = hash(deps) ^ session_seed;
    // Multiple computations with same deps → same node → deduplicated
}
```
**Apply to Havel**: Fine-grained queries like "type of expression at span" without explicit keys.

### G. Parallel Query Execution with Latches
```rust
// Havel can use similar pattern for parallel module compilation:
struct QueryLatch {
    complete: AtomicBool,
    waiters: Mutex<Vec<Waiter>>,
    condvar: Condvar,
}
// Waiters block on condvar, notified on completion
```

### H. Command-Line Hash for Cache Invalidation
```rust
// Hash all compiler flags affecting output:
// - Optimization level
// - Target triple
// - Feature flags
// - Include paths
// Store in cache header; mismatch → full rebuild
```

## Recommended Havel Architecture

### Phase 1: Query System Foundation
1. Define `QueryContext` trait for Havel compiler context
2. Implement `QueryCache` trait with `DefIdCache` for modules, `SingleCache` for globals
3. Create `DepGraph` with `DepNode = (DepKind, Fingerprint)`
4. Implement `try_mark_green` / red-green algorithm

### Phase 2: Incremental Compilation
1. Add on-disk cache (serialize bytecode + type info + fingerprints)
2. Implement work product tracking for `.hvb` files
3. Add command-line hash for cache invalidation
4. Implement `try_load_from_disk` with fingerprint verification

### Phase 3: Parallel Execution
1. Add `QueryLatch` with condvar for cross-thread waiting
2. Integrate with thread pool (Rayon or custom)
3. Implement cycle detection/breaking for parallel queries
4. Add query stack tracking for debugging

### Phase 4: Advanced Features
1. Anonymous queries for fine-grained incremental (e.g., per-function)
2. Side-effect tracking for diagnostics
3. Incremental verification mode (`-Z incremental-verify-ich` equivalent)
4. Query profiling / self-profiling integration

## Critical Implementation Details

### Fingerprint Stability
- Use **stable hashing** (not `Hash`) for cross-session fingerprints
- Hash `DefPathHash` equivalents, not `DefId` (which change per session)
- Include session-specific seed for anonymous nodes

### Memory Efficiency
- **Sharded hashmaps** for concurrent cache access
- **Inline edge storage** (small vec) for dep-graph edges
- **IndexVec** for dense integer-indexed data
- **Arc<DepGraphData>** for shared read-only access

### Correctness Guarantees
- **Red-green invariant**: Green node = all deps green + fingerprint matches
- **Cycle soundness**: Cycle detection only on active queries, not cached results
- **Verification**: Random sampling (1/32) + optional full verification of loaded fingerprints
- **Error resilience**: Compilation errors don't corrupt incremental cache

### Performance Optimizations
- **Virtual dep-node indices** for non-incremental mode (profiling only)
- **Eval-always queries**: Skip caching/dep-tracking for always-recompute queries
- **Feedable queries**: Allow feeding values to break cycles (const generics)
- **Depth limiting**: Prevent stack overflow in deep query chains

## Summary

The Rust query system provides a **mature, production-tested architecture** for demand-driven incremental compilation. Key takeaways for Havel:

1. **Implicit dependency tracking** via TLS is simpler and less error-prone than explicit graphs
2. **Red/green marking** with fingerprint comparison is the gold standard for invalidation
3. **Three-tier caching** (memory → disk → recompute) balances speed and correctness
4. **Parallel execution** requires careful latch-based synchronization and cycle breaking
5. **Specialized caches** (DefIdCache, VecCache, etc.) provide significant memory/perf wins
6. **Work product tracking** enables artifact reuse beyond just query results

Havel should start with a simplified version (single-threaded, memory-only cache) and incrementally add disk cache, parallel execution, and advanced features.
Thought: 1.0s
I've created a comprehensive report. Let me provide a summary to the user.
I've analyzed the Rust compiler's query system and incremental compilation infrastructure. Here's a summary of the key insights:
Key Findings
1. Query-Based Demand-Driven Model
- Queries are pure functions Key → Value with automatic memoization
- Implicit dependency tracking via thread-local TaskDeps - calling a query automatically records the edge
- Parallel execution with QueryLatch (condvar-based) for cross-thread waiting
- Cycle detection via DFS on active query wait-for graph, with deterministic breaking
2. Incremental Compilation via Red/Green Marking
- DepNode = (DepKind, Fingerprint) uniquely identifies query invocation
- Previous session's graph is immutable; current session builds new graph referencing it
- try_mark_green(node) recursively validates all transitive dependencies
- Green = fingerprint matches; Red = fingerprint differs or dependency red
3. Three-Tier Caching
Tier	Storage	Purpose
In-memory	QueryCache (sharded hashmap)	Fast lookup per session
On-disk	Serialized query cache file	Persists across sessions
Recompute	Provider function	Fallback when cache invalid
4. Salsa-like Architecture with Differences
Similar to Salsa	Different from Salsa
Query config + provider	Two-phase graph (prev/current)
Implicit dep tracking	Explicit DepNode construction
Memoization	Anonymous queries (hash of deps)
Invalidation	Side effects as special dep-nodes
Parallel execution	Work products for artifacts
5. Techniques for Havel
- Phase 1: QueryContext + QueryCache (DefIdCache for modules, SingleCache for globals) + DepGraph with red/green
- Phase 2: On-disk cache with fingerprint verification + work product tracking (.hvb files)
- Phase 3: Parallel execution with latches + cycle breaking
- Phase 4: Anonymous queries, side effects, incremental verification

  1. LLVM Integration via rustc_llvm Crate
Pattern: Rust uses a separate rustc_llvm crate (a fork of LLVM) with custom FFI bindings in llvm/ffi.rs and wrapper functions in llvm/mod.rs.
Key files:
- llvm/ffi.rs — Raw C API bindings (~3000 lines, auto-generated from LLVM headers)
- llvm/mod.rs — Safe Rust wrappers, attribute helpers, module flags, diagnostics
- llvm_util.rs — LLVM initialization, target features, pass management
For Havel: Use LLVM's C API directly via a thin wrapper (like llvm-sys or custom bindings). Don't fork LLVM unless you need custom passes.
2. Codegen Pipeline: MIR → LLVM IR
Phase 1: CGU (Codegen Unit) Creation (base.rs:56-144)
fn compile_codegen_unit(tcx, cgu_name) -> (ModuleCodegen, u64) {
    let llvm_module = ModuleLlvm::new(tcx, cgu_name);  // Creates LLVM Context + Module
    let cx = CodegenCx::new(tcx, cgu, &llvm_module);

    // 1. PREDEFINE: Declare all mono items (functions, statics) with correct linkage
    for mono_item in mono_items {
        mono_item.predefine(&mut cx, ...);
    }

    // 2. DEFINE: Fill in function bodies
    for mono_item in mono_items {
        mono_item.define(&mut cx, ...);
    }

    // 3. Finalize: debug info, coverage, llvm.used globals
    ModuleCodegen::new_regular(cgu_name, llvm_module)
}
Key insight: Two-pass approach — predefine all symbols first (for cross-references), then define bodies. Each CGU gets its own LLVMContext for parallelism.
Phase 2: Optimization (back/write.rs:555-704)
llvm_optimize(module, opt_level, opt_stage, autodiff_stage) {
    // opt_stage: PreLinkNoLTO | PreLinkFatLTO | PreLinkThinLTO | ThinLTO | FatLTO
    // autodiff_stage: PreAD | DuringAD | PostAD
    LLVMRustOptimize(module, target_machine, opt_level, opt_stage, ...)
}
Optimization stages:
Stage	Purpose
PreLinkNoLTO	Regular per-module optimization
PreLinkThinLTO	Optimize + emit ThinLTO bitcode summary
PreLinkFatLTO	Optimize + emit full bitcode for merging
ThinLTO	Post-link per-module optimization (after import)
FatLTO	Post-link optimization on merged monolithic module
3. LTO / ThinLTO Architecture
Fat LTO (back/lto.rs:198-370)
1. Pick costliest module as base (avoids serializing largest CGU)
2. Link all other modules into base via LLVMRustLinkerAdd
3. Internalize symbols below export threshold (LLVMRustRunRestrictionPass)
4. Run full optimization pipeline on merged module
Thin LTO (back/lto.rs:402-587)
1. Create summaries for each module (ThinLTOBuffer with index)
2. Global analysis: LLVMRustCreateThinLTOData merges summaries into index
3. Per-module optimization (parallelizable):
- LLVMRustPrepareThinLTORename — resolve cross-module references
- LLVMRustPrepareThinLTOResolveWeak — handle weak symbols
- LLVMRustPrepareThinLTOInternalize — internalize non-exported
- LLVMRustPrepareThinLTOImport — import optimized functions from other modules
- Run ThinLTO pass manager
4. Caching: LTO cache keys saved to disk for incremental compilation
For Havel JIT: ThinLTO is more relevant — allows parallel per-module optimization and incremental caching. The summary-based approach avoids monolithic merge bottleneck.
4. Optimization Pass Control
Pass selection in back/write.rs:660-700:
LLVMRustOptimize(
    module,
    target_machine,
    to_pass_builder_opt_level(opt_level),  // O0/O1/O2/O3/Os/Oz
    opt_stage,
    config.no_prepopulate_passes,          // Skip default passes
    config.verify_llvm_ir,
    config.lint_llvm_ir,
    merge_functions,
    unroll_loops,
    vectorize_slp,
    vectorize_loop,
    extra_passes: config.passes.join(","),  // User-specified passes
    llvm_plugins: config.llvm_plugins.join(","),
    ...
)
Key controls for JIT:
- no_prepopulate_passes — Build custom pass pipeline
- opt_stage — Choose pipeline variant
- extra_passes — Inject custom passes
- llvm_plugins — Load dynamic pass plugins
5. Module & Context Management
ModuleLlvm (lib.rs:390-460):
struct ModuleLlvm {
    llcx: &'static mut llvm::Context,      // Owned context
    llmod_raw: *const llvm::Module,         // Module in that context
    tm: ManuallyDrop<OwnedTargetMachine>,   // Target machine (dispose first!)
}
Lifetime rule: TargetMachine must be dropped before Context (UAF prevention via ManuallyDrop).
For Havel JIT:
- Reuse LLVMContext across compilations (thread-local or pool)
- LLVMModule can be serialized to bitcode for caching
- Use LLVMContextCreate() / LLVMContextDispose() explicitly
6. Key Patterns for Havel's LLVM JIT
Rust Pattern	Havel Adaptation
Per-CGU LLVMContext	Single context + module pool for JIT
LLVMRustOptimize C++ wrapper	Direct LLVMPassManager API or llvm::PassBuilder
ThinLTO summaries for incremental	Cache optimized bitcode + summaries per function
ModuleBuffer / ThinBuffer serialization	LLVMWriteBitcodeToMemoryBuffer for caching
LLVMRustLinkerAdd for LTO	llvm::Linker for JIT module merging
Diagnostic handlers per stage	Custom diagnostic callback for JIT errors
Target machine per module	Single target machine (host) for JIT
7. Critical Implementation Details
Data Layout: Set from TargetMachine (context.rs:212-230):
let tm = create_informational_target_machine(sess, false);
LLVMRustSetDataLayoutFromTargetMachine(llmod, tm.raw());
Module Flags (context.rs:267-520): 30+ flags for sanitizers, CFI, LTO, branch protection, etc.
Target Features (llvm_util.rs:231-332): Complex mapping from Rust -C target-feature to LLVM feature names with version checks and dependency resolution.
For Havel: Hardcode data layout for host target; simplify feature handling to explicit opt-in.
Summary for Havel
1. Start simple: Single LLVMContext, LLVMModule, TargetMachine. Build IR directly via C API.
2. JIT pipeline: Parse → Type-check → Lower to LLVM IR → LLVMPassManager (O2) → LLVMExecutionEngine (ORC JIT)
3. Caching: Serialize optimized modules to bitcode (LLVMWriteBitcodeToMemoryBuffer), cache by hash.
4. Incremental: ThinLTO-style summaries if you need cross-module optimization.
5. Pass control: Use LLVMPassBuilder with PassBuilderOptLevel + custom pipeline for JIT vs AOT.
The Rust codegen is AOT-optimized (CGU parallelism, incremental, LTO). For JIT, prioritize: fast IR generation, minimal passes, ORC JIT with lazy compilation, bitcode caching.

Key Insights from rustc_codegen_cranelift JIT Integration
1. Cranelift JIT Integration Architecture
Two Backend Modes (AOT vs JIT)
- AOT (aot.rs): Uses cranelift_object → ObjectModule → writes .o files for linking
- JIT (jit.rs): Uses cranelift_jit → JITModule → executes in-memory
// lib.rs:252-260 - Mode selection
if config.jit_mode {
    driver::jit::run_jit(tcx, config.jit_args);
} else {
    driver::aot::run_aot(tcx, metadata, need_metadata_module)
}
JIT Module Setup (jit.rs:19-33)
fn create_jit_module(tcx: TyCtxt<'_>) -> (UnwindModule<JITModule>, CodegenCx) {
    let isa = crate::build_isa(tcx.sess, true);  // jit=true
    let mut jit_builder = JITBuilder::with_isa(isa, cranelift_module::default_libcall_names());

    // Register host functions (compiler builtins + allocator)
    crate::compiler_builtins::register_functions_for_jit(&mut jit_builder);

    // Dynamic symbol resolution from dylibs
    jit_builder.symbol_lookup_fn(dep_symbol_lookup_fn(tcx.sess, crate_info));

    let mut jit_module = UnwindModule::new(JITModule::new(jit_builder), false);
    // ...
}
2. Differences from LLVM Backend
Aspect	LLVM (rustc_codegen_llvm)	Cranelift (rustc_codegen_cranelift)
JIT Engine	llvm::jit (ORC JIT)	cranelift_jit (lightweight, no ORC)
Codegen Units	ThinLTO + parallel CGUs	No LTO; sequential or parallel CGUs
Incremental	Full support	Limited (CG_CLIF_DISABLE_INCR_CACHE)
Libcalls	Built-in	Must register via JITBuilder::symbol()
Dynamic Loading	dlopen/LoadLibrary	libloading crate + custom symbol_lookup_fn
Stack Probes	LLVM handles	Manual enable_probestack + probestack_strategy
Key Simplifications vs LLVM:
- No ORC layer complexity — Cranelift JIT is a thin wrapper around JITModule
- No bitcode/IR persistence — JIT compiles directly to machine code
- Simpler symbol resolution — Direct libloading + dlsym iteration
3. JIT-Specific Optimizations
ISA Flags for JIT (lib.rs:290-360)
fn build_isa(sess: &Session, jit: bool) -> Arc<dyn TargetIsa> {
    // JIT-specific: disable PIC (no relocation needed)
    flags_builder.set("is_pic", if jit { "false" } else { "true" });

    // Optimization level mapping
    match sess.opts.optimize {
        OptLevel::No => flags_builder.set("opt_level", "none"),
        _ => flags_builder.set("opt_level", "speed_and_size"),  // JIT uses same
    }

    // Stack probes for stack growth (critical for JIT)
    flags_builder.enable("enable_probestack");
    flags_builder.set("probestack_strategy", "inline");
}
Peephole Optimizations (optimize/peephole.rs)
- Boolean Not unwrap: icmp_imm(equal, x, 0) → direct use
- Constant branch folding: brif(iconst(0), ...) → static elimination
Function Compilation Pipeline (base.rs:137-245)
pub(crate) fn compile_fn(cx, profiler, cached_context, module, codegened_func) {
    // Reuse context to avoid allocations
    context.clear();
    context.func = codegened_func.func;
    context.want_disasm = cx.should_write_ir;

    // Single define_function call — Cranelift compiles + emits in one step
    module.define_function(codegened_func.func_id, context)?;
}
4. Dynamic Code Loading (Symbol Resolution)
Dependency Symbol Lookup (jit.rs:156-201)
fn dep_symbol_lookup_fn(sess: &Session, crate_info: CrateInfo)
    -> Box<dyn Fn(&str) -> Option<*const u8> + Send>
{
    // 1. Load all dependent dylibs in postorder (deps first)
    let mut dylib_paths = Vec::new();
    for &cnum in crate_info.used_crates.iter().rev() {
        match data[cnum] {
            Linkage::Dynamic => dylib_paths.push(src.dylib.unwrap().0.clone()),
            Linkage::Static => fatal("JIT mode only supports dylibs"),
        }
    }

    // 2. Leak libraries to keep them alive (no unload in JIT)
    let imported_dylibs = Box::leak(
        dylib_paths.into_iter()
            .map(|path| unsafe { libloading::Library::new(&path).unwrap() })
            .collect::<Box<[_]>>()
    );

    // 3. Return closure that searches all loaded dylibs
    Box::new(move |sym_name| {
        for dylib in &*imported_dylibs {
            if let Ok(sym) = unsafe { dylib.get::<*const u8>(sym_name.as_bytes()) } {
                return Some(*sym);
            }
        }
        None
    })
}
Compiler Builtins Registration (compiler_builtins.rs:11-35)
macro_rules! builtin_functions {
    ($register:ident; $(fn $name:ident(...) -> _;)*) => {
        #[cfg(feature = "jit")]
        extern "C" { $(fn $name(...);)* }

        #[cfg(feature = "jit")]
        pub(crate) fn $register(builder: &mut JITBuilder) {
            for (name, val) in [$( (stringify!($name), $name as *const u8), )*] {
                builder.symbol(name, val);  // Register host symbol
            }
        }
    }
}
5. Techniques Relevant for Havel's JIT
A. Module Abstraction (cranelift_module::Module trait)
Both ObjectModule (AOT) and JITModule implement the same trait. Havel should:
trait CodegenModule {
    fn declare_function(&mut self, name: &str, linkage: Linkage, sig: &Signature) -> FuncId;
    fn define_function(&mut self, func_id: FuncId, ctx: &mut Context) -> Result<()>;
    fn get_finalized_function(&self, func_id: FuncId) -> *const u8;  // JIT only
    fn target_config(&self) -> TargetFrontendConfig;
}
B. Context Reuse for Compilation (base.rs:140-152)
let mut cached_context = Context::new();
for func in functions {
    cached_context.clear();
    cached_context.func = func;
    module.define_function(func_id, &mut cached_context)?;
}
→ Havel: Reuse a single Context across compilations to avoid allocations.
C. Host Function Registration Pattern
// Register host functions BEFORE compiling user code
jit_builder.symbol("host_fn_name", host_fn_ptr as *const u8);

// In Cranelift IR: call_indirect to host function via FuncRef
→ Havel: Register all host functions (IO, hotkeys, window mgmt) upfront in JITBuilder.
D. JIT Entry Point Execution (jit.rs:94-120)
let start_func_id = jit_module.declare_function("main", Linkage::Import, &start_sig)?;
let finalized_start: *const u8 = jit_module.get_finalized_function(start_func_id);
let f: extern "C" fn(c_int, *const *const c_char) -> c_int =
    unsafe { std::mem::transmute(finalized_start) };
std::process::exit(f(argc, argv));
→ Havel: Similar pattern for executing JIT'd entry point or hot-reloaded functions.
E. Unwind Info Registration (unwind_module.rs:34-40)
#[cfg(feature = "jit")]
impl UnwindModule<JITModule> {
    pub(crate) fn finalize_definitions(mut self) -> JITModule {
        self.module.finalize_definitions().unwrap();
        unsafe { self.unwind_context.register_jit(&self.module) };  // Register EH frames
        self.module
    }
}
→ Havel: If using exceptions/unwinding, must register unwind info with JIT.
F. Incremental Compilation Considerations
- Cranelift JIT does not support incremental — full recompile each time
- AOT path has CguReuse logic but JIT bypasses it entirely
- Havel: Design for fast full recompile (Cranelift is fast enough) or cache compiled functions
6. Summary: Havel JIT Design Recommendations
Recommendation	Rationale
Use cranelift_jit crate directly	Thin, fast, no ORC complexity
Abstract Module trait	Same codegen for AOT + JIT
Pre-register all host functions	Via JITBuilder::symbol() before codegen
Reuse Context	Avoid allocation overhead in hot path
Use libloading for dynamic deps	Load dylibs, resolve symbols at runtime
Disable PIC for JIT	is_pic = false — no relocations needed
Enable inline stack probes	Required for stack growth in JIT'd code
Register unwind info	If Havel uses panic/unwind across JIT boundary
No incremental in JIT	Accept full recompile; Cranelift is ~10x faster than LLVM
Separate JIT/AOT entrypoints	Like run_jit vs run_aot in driver

1. MIR Structure — What Havel Already Has, What's Missing
Rust MIR Feature	Havel Status	Gap / Opportunity
Structured CFG with BasicBlock (indexed, no branches inside)	✅ Has BytecodeChunk with linear instructions	Havel uses flat instruction array + jumps. Consider explicit BasicBlock structure for optimization passes
Locals typed (LocalDecl with Ty, mutability, LocalInfo)	⚠️ Partial	Havel Value is type-erased at runtime. Add LocalDecl metadata for borrow-check-style analysis
Places (Place = Local + Projection[]) with Deref, Field, Index, Downcast	❌ Missing	Havel uses stack-only operands. Adding place abstraction enables: load/store optimization, escape analysis, NRVO
Terminators (explicit control flow: Goto, SwitchInt, Call, Return, Drop, Assert)	❌ Implicit via jump targets	Explicit terminators simplify: dead code elimination, jump threading, unwind handling
SourceInfo (span + scope) on every statement/terminator	⚠️ Debug-only	Propagate source locations through bytecode for better error reporting
MirPhase (Built → Analysis → Runtime/Optimized)	❌ Single phase	Add phase tracking to enable staged optimizations
2. MIR Builder Pattern (Rust) — Directly Applicable
Rust's Builder in rustc_mir_build/src/builder/mod.rs uses:
BlockAnd monad — packages BasicBlock + produced value, with unpack! macro for chaining
CFG abstraction — start_new_block(), push(), terminate(), goto()
Scope management — lexical scopes + drop scopes + source scopes (separate concerns)
Temporaries with fixed indexes — for pattern guards, deduplication
Havel action: Refactor bytecode emission into a BytecodeBuilder with:
struct BlockAnd<T> { BasicBlock bb; T value; };
// Emit: auto [bb, val] = builder.expr(expr).into_block();
3. Monomorphization — Key Patterns for Havel Generics
Rust's collector (rustc_monomorphize/src/collector.rs):
Two-phase graph traversal:
Discover roots (public non-generic items, #[inline] functions)
Walk uses from MIR: calls, fn refs, drop glue, vtable methods, unsizing casts
Instantiation modes: GloballyShared (one copy, exported) vs LocalCopy (inlined per CGU)
UsageMap — bidirectional graph: used_map[item] = callees, user_map[item] = callers
Eager vs Lazy strategy: Eager for incremental stability (all drop glue, default impls); Lazy for minimal codegen
Havel application:
// Monomorphization collector for generic functions
struct MonoCollector {
  roots: Set<FunctionId>           // Entry points + exported
  used: Map<FunctionId, Set<FunctionId>>  // Call graph
  mentioned: Set<FunctionId>       // For const-eval safety

  fn collect_from_mir(body: &BytecodeBody) {
    for inst in body.instructions {
      match inst.opcode {
        CALL | CALL_DYN => record_use(inst.callee)
        LOAD_GLOBAL (fn) => record_fn_ref(inst.fn)
        DROP => record_drop_glue(inst.type)
        UNSIZE_CAST => record_vtable_methods(inst.trait)
      }
    }
  }
}
4. Partitioning / Codegen Units — For Havel JIT Tiers
Rust's partitioning.rs:
CGU per module: "stable" (non-generic) + "volatile" (monomorphized) split
Merge by inlined overlap — minimizes duplicate inlined code
Internalization — make symbols Internal linkage if not used cross-CGU
Size estimates drive merging
Havel JIT tiering: Use similar logic for:
Tier 0: Interpreter
Tier 1: Baseline JIT (per-function, LocalCopy equivalent)
Tier 2: Optimizing JIT (hot functions, GloballyShared equivalent)
Partition by module for incremental re-compilation
5. MIR Optimization Passes — Prioritized for Havel
From rustc_mir_transform/src/ — ordered by impact/feasibility:
Pass	Havel Value	Effort
simplify / simplify_branches	Remove dead blocks, merge trivial jumps	Low
copy_prop / dest_prop	Propagate constants, eliminate copies	Medium
dead_store_elimination	Remove unused STORE_VAR/STORE_GLOBAL	Medium
gvn (Global Value Numbering)	CSE across blocks	High
jump_threading	Short-circuit conditional jumps	Medium
sroa (Scalar Replacement)	Break aggregates into scalars	High
inline	Inline small functions (with #[inline] hint)	High
nrvo (Named Return Value Opt)	Avoid return copies	Medium
promote_consts	Hoist constants out of loops	Low
remove_unneeded_drops	Elide unnecessary drops	Medium
elaborate_drops	Expand drops to calls (pre-optimization)	Required for correctness
Havel priority order:
simplify + simplify_branches (immediate CFG cleanup)
copy_prop + dest_prop (SSA-like local optimization)
dead_store_elimination (stack/locals cleanup)
jump_threading (control flow)
promote_consts (loop invariants)
inline (with backedge counter guidance)
6. Dataflow Framework — Reusable Infrastructure
Rust uses a generic dataflow engine (rustc_data_structures::dataflow). Havel could adopt:
// Generic forward/backward dataflow
template<typename Domain>
class DataflowAnalyzer {
  using TransferFn = Domain(*)(Domain, Instruction);
  Domain analyze(Body& body, TransferFn transfer, Domain init);
};

// Instances:
// - LivenessAnalysis (backward)
// - ConstProp (forward)
// - TypeFeedback (forward, per-instruction)
// - BorrowCheck (custom, if adding ownership)
7. MIR Validation — Critical for Reliability
Rust runs validate pass after every MIR transform. Havel should add:
class BytecodeValidator {
  bool validate(const BytecodeChunk& chunk) {
    // 1. All jump targets valid
    // 2. Stack height consistent at merge points
    // 3. Locals initialized before use
    // 4. No unreachable code (optional)
    // 5. Terminator on every block
  }
};
Run in debug builds after each optimization pass.
8. "Mentioned Items" — Const-Eval Safety
Rust's mentioned_items pass collects items that syntactically appear (even in dead code) to ensure const-eval errors aren't optimization-dependent. Havel needs this for:
Compile-time constant evaluation errors
Type checking in generic contexts
9. Coroutine/Async State Machine — Directly Relevant
Rust's CoroutineInfo + StateTransform pass:
Lowers yield/await to explicit state machine
Stores: yield_ty, resume_ty, coroutine_drop, coroutine_layout
Havel's YIELD/AWAIT opcodes already do this at bytecode level — consider MIR-level transform for optimization
10. Drop Elaboration — Required for Correctness
Rust's elaborate_drops expands implicit drops into explicit Drop terminators with unwind edges. Havel's GC handles memory but:
Resource cleanup (files, sockets, timers) needs explicit drop elaboration
Add Drop terminator + unwind handling before optimization passes
Recommended Havel Roadmap
Phase	Task	Rust Reference
1. CFG Restructure	Introduce BasicBlock, explicit terminators, BytecodeBuilder	rustc_mir_build/src/builder/cfg.rs
2. Local Metadata	Add LocalDecl with type, mutability, debug info	rustc_middle/src/mir/mod.rs:LocalDecl
3. Place Abstraction	Place = Local + Projection[] for load/store optimization	rustc_middle/src/mir/statement.rs:Place
4. Pass Manager	Pipeline: validate → simplify → copy_prop → dead_store → jump_threading	rustc_mir_transform/src/pass_manager.rs
5. Monomorphization	Collect generic instantiations from bytecode call graph	rustc_monomorphize/src/collector.rs
6. Tiered JIT Partitioning	Map CGU logic to JIT tiers (interpreter → baseline → opt)	rustc_monomorphize/src/partitioning.rs
7. Const/Generic Safety	Mentioned-items pass for optimization-independent errors	rustc_mir_transform/src/mentioned_items.rs
Code Patterns to Adopt Directly
BlockAnd monad for ergonomic builder
IndexVec for dense local/block storage (use rustc_index equivalent or Vec with newtype indices)
SourceInfo on every instruction
UsageMap for call graph + inlining decisions
Computed-goto dispatch (already in Havel — good!)
Validation after every pass (critical for compiler correctness)
Phase tracking on bytecode bodies
# Todos
Report MIR design insights for Havel compiler
Thought: 229ms
The task is complete. I've provided a comprehensive report on the key insights from Rust's MIR design and monomorphization that can be applied to Havel's bytecode compiler.
The analysis is complete. I've explored the key Rust MIR files and compared them with Havel's current bytecode compiler architecture. The report above details:
MIR structure gaps — Havel has flat instructions; Rust has structured CFG with explicit terminators, typed locals, and Places
Builder pattern — Rust's BlockAnd monad + CFG abstraction is directly adoptable for Havel's bytecode emission
Monomorphization — Two-phase graph traversal (roots → uses) with UsageMap for call graph + inlining decisions
Partitioning — CGU split by module + merge by inlined overlap maps to JIT tiering
Optimization passes — Prioritized list with effort estimates
Dataflow framework — Generic infrastructure for liveness, const-prop, type feedback
Validation — Critical after every pass (Rust does this)
Const-eval safety — "Mentioned items" pass prevents optimization-dependent errors
Coroutines — State machine lowering at MIR level (Havel does at bytecode level)
Drop elaboration — Required for resource cleanup correctness
The roadmap table gives a phased approach for Havel to adopt these patterns incrementally

