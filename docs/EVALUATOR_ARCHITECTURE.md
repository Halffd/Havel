# Evaluator Architecture - Next Phase Plan

## Current State (After Visitor Extraction)

```
runtime/
├── Interpreter.cpp          # 3,251 lines (target: 800-1,200)
├── Environment.hpp/cpp      # Variable scoping
└── evaluator/
    ├── ExprEvaluator.cpp    # 1,177 lines ⚠️ approaching threshold
    └── StatementEvaluator.cpp # 1,112 lines ⚠️ approaching threshold
```

**Warning Signs:**
- Evaluator files approaching 1,500-2,000 line threshold
- Risk: Recreating monolith in two pieces
- Config logic mixed with runtime execution

---

## Phase 1: Remove Remaining Delegates from Interpreter

### Priority 1: Expression Visitors (move to ExprEvaluator)

| Visitor | Lines | Priority | Destination |
|---------|-------|----------|-------------|
| `visitIdentifier` | ✅ Done | - | ExprEvaluator |
| `visitBacktickExpression` | ~15 | High | modules/process/ShellExecutor |
| `visitShellCommandExpression` | ~30 | High | modules/process/ShellExecutor |

### Priority 2: Statement Visitors (move to modules)

| Visitor | Lines | Priority | Destination |
|---------|-------|----------|-------------|
| `visitShellCommandStatement` | ~80 | High | modules/process/ShellExecutor |
| `visitInputStatement` | ~50 | High | modules/io/InputModule |
| `visitStructDeclaration` | ~80 | Medium | runtime/types/StructRuntime |
| `visitEnumDeclaration` | ~20 | Medium | runtime/types/EnumRuntime |
| `visitTraitDeclaration` | ~30 | Medium | runtime/types/TraitRuntime |
| `visitImplDeclaration` | ~50 | Medium | runtime/types/TraitRuntime |

### Priority 3: Config Visitors (separate config from runtime)

| Visitor | Lines | Priority | Destination |
|---------|-------|----------|-------------|
| `visitConfigBlock` | ~40 | Low | runtime/config/ConfigEvaluator |
| `visitDevicesBlock` | ~30 | Low | runtime/config/ConfigEvaluator |
| `visitModesBlock` | ~30 | Low | runtime/config/ConfigEvaluator |
| `visitConfigSection` | ~30 | Low | runtime/config/ConfigEvaluator |

---

## Phase 2: Extract Runtime Services

### 2.1 Call Dispatcher

**Problem:** `visitCallExpression` contains:
- Built-in function calls
- User function calls
- Struct constructors (`__call__`)
- Method binding

**Solution:**
```
runtime/dispatch/
├── CallDispatcher.hpp/cpp    # Unified call handling
├── BuiltinResolver.hpp       # Built-in function lookup
├── FunctionResolver.hpp      # User function lookup
└── MethodResolver.hpp        # Method binding (__call__, struct methods)
```

**Impact on ExprEvaluator:**
```cpp
// Before (150+ lines in visitCallExpression)
if (auto* builtin = callee.get_if<BuiltinFunction>()) { ... }
else if (auto* userFunc = ...) { ... }
else if (auto* objPtr = ...) { ... }

// After (5 lines)
return callDispatcher.dispatch(callee, args, node);
```

### 2.2 Member Resolver

**Problem:** `visitMemberExpression` handles:
- Object field access
- Array methods (`.length`, `.push()`)
- String methods (`.upper()`, `.replace()`)
- Struct field access
- Struct method binding
- Trait method resolution

**Solution:**
```
runtime/dispatch/
└── MemberResolver.hpp/cpp    # Unified member access
```

**Impact on ExprEvaluator:**
```cpp
// Before (150+ lines in visitMemberExpression)
if (object.is<HavelObject>()) { ... }
else if (object.is<HavelArray>()) { ... }
else if (object.is<HavelStructInstance>()) { ... }

// After (5 lines)
return memberResolver.resolve(object, propName, node);
```

### 2.3 Type Registry

**Problem:** Struct/enum/trait logic scattered across:
- `visitStructDeclaration` (Interpreter.cpp)
- `visitEnumDeclaration` (Interpreter.cpp)
- `visitImplDeclaration` (Interpreter.cpp)
- `HavelStructType` (Environment.hpp)

**Solution:**
```
runtime/types/
├── TypeRegistry.hpp/cpp      # Central type registry
├── StructRuntime.hpp/cpp     # Struct creation, field access
├── EnumRuntime.hpp/cpp       # Enum creation, variant matching
└── TraitRuntime.hpp/cpp      # Trait impl resolution
```

---

## Phase 3: Split Large Evaluators

### 3.1 ExprEvaluator Split (when >1,500 lines)

```
evaluator/
├── ExprEvaluator.hpp         # Common base + helpers
├── expressions/
│   ├── BinaryExpr.cpp        # Binary operations
│   ├── CallExpr.cpp          # Function calls (uses CallDispatcher)
│   ├── MemberExpr.cpp        # Member access (uses MemberResolver)
│   ├── LiteralExpr.cpp       # Literals (string, number, array, object)
│   ├── ControlExpr.cpp       # If, ternary, block expressions
│   └── SpecialExpr.cpp       # Backtick, shell, import
└── StatementEvaluator.cpp    # Unchanged for now
```

### 3.2 StatementEvaluator Split (when >1,500 lines)

```
evaluator/
├── StatementEvaluator.hpp    # Common base + helpers
├── statements/
│   ├── ControlFlow.cpp       # If, while, for, loop, switch
│   ├── Declarations.cpp      # Let, const, function
│   ├── Modules.cpp           # Import, use, with
│   ├── Config.cpp            # Config, devices, modes sections
│   └── Special.cpp           # Hotkey binding, sleep, repeat
└── (ExprEvaluator files)     # Unchanged
```

---

## Phase 4: Config/Runtime Separation

### Current Problem

`StatementEvaluator` mixes:
- Runtime execution (`visitForStatement`, `visitIfStatement`)
- Config processing (`visitConfigBlock`, `visitDevicesBlock`)

These have different semantics:
- **Runtime**: Execute immediately, produce values
- **Config**: Declare settings, stored for later use

### Solution

```
runtime/
├── evaluator/                # Runtime execution only
│   ├── ExprEvaluator
│   └── StatementEvaluator
└── config/                   # Config DSL evaluation
    ├── ConfigEvaluator.hpp/cpp
    ├── ConfigAST.hpp         # Config-specific AST nodes
    └── ConfigRegistry.hpp    # Config value storage
```

---

## Target Architecture

```
runtime/
├── Interpreter.cpp           # ~800-1,200 lines (orchestration only)
│   ├── parse()
│   ├── execute()
│   └── evaluate(node) → dispatches to evaluators
│
├── evaluator/                # Expression/statement execution
│   ├── ExprEvaluator/
│   │   ├── BinaryExpr.cpp
│   │   ├── CallExpr.cpp
│   │   ├── MemberExpr.cpp
│   │   ├── LiteralExpr.cpp
│   │   └── ControlExpr.cpp
│   └── StatementEvaluator/
│       ├── ControlFlow.cpp
│       ├── Declarations.cpp
│       └── Modules.cpp
│
├── dispatch/                 # Call/member resolution
│   ├── CallDispatcher.cpp
│   └── MemberResolver.cpp
│
├── types/                    # Type system
│   ├── TypeRegistry.cpp
│   ├── StructRuntime.cpp
│   ├── EnumRuntime.cpp
│   └── TraitRuntime.cpp
│
├── config/                   # Config DSL (separate from runtime)
│   ├── ConfigEvaluator.cpp
│   └── ConfigRegistry.cpp
│
└── services/                 # Runtime services
    ├── FunctionDispatcher.cpp
    ├── ModuleLoader.cpp
    └── Environment.cpp
```

---

## Metrics & Thresholds

| Component | Current | Warning | Target |
|-----------|---------|---------|--------|
| Interpreter.cpp | 3,251 | 2,000 | 800-1,200 |
| ExprEvaluator.cpp | 1,177 | 1,500 | 400-600 (per file) |
| StatementEvaluator.cpp | 1,112 | 1,500 | 400-600 (per file) |

**Split Triggers:**
- Single file >1,500 lines → Split by category
- Single function >100 lines → Extract to service
- Multiple visitors doing similar work → Create dispatcher

---

## Immediate Next Steps

1. **Move shell/input to modules** (high priority)
   - `visitBacktickExpression` → `modules/process/ShellExecutor`
   - `visitShellCommandStatement` → `modules/process/ShellExecutor`
   - `visitInputStatement` → `modules/io/InputModule`

2. **Create CallDispatcher** (medium priority)
   - Extract call logic from `visitCallExpression`
   - Reduces ExprEvaluator by ~150 lines

3. **Create MemberResolver** (medium priority)
   - Extract member logic from `visitMemberExpression`
   - Reduces ExprEvaluator by ~150 lines

4. **Move type system to runtime/types/** (low priority)
   - Struct/enum/trait declarations
   - Separates type concerns from evaluation

---

## Design Principles

1. **Interpreter orchestrates, doesn't execute**
   - `Interpreter::evaluate()` dispatches to evaluators
   - No AST logic in Interpreter.cpp

2. **Evaluators execute, don't resolve**
   - `ExprEvaluator` calls `CallDispatcher`, doesn't implement calls
   - `ExprEvaluator` calls `MemberResolver`, doesn't implement member access

3. **Services handle complexity**
   - Call dispatch, member resolution, type registry
   - Each service is independently testable

4. **Config separate from runtime**
   - Config evaluation has different semantics
   - Config values stored, runtime values executed

5. **Files stay under 1,500 lines**
   - Split by category when threshold approached
   - Prevents recreating monolith

---

## Inspiration

This architecture follows successful interpreters:

| Language | Structure | Lesson |
|----------|-----------|--------|
| **Lua** | `lvm.c` (VM), `lobject.c` (objects) | Separate execution from objects |
| **CPython** | `ceval.c` (eval), `object.c` (objects) | Eval dispatches to object methods |
| **Ruby MRI** | `vm_insnhelper.c`, `vm_method.c` | Dispatch helpers separate from eval |

**Key Insight:** All successful interpreters eventually split into:
1. **Evaluator** (tree walk / bytecode exec)
2. **Dispatcher** (call/member resolution)
3. **Objects** (runtime types)
4. **Services** (modules, environment)

We're on the right path - just need to continue the split.
