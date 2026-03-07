# Progress Update

## Latest Changes

### SystemModule Split ✅
- **Before:** 2,081 lines (monolithic)
- **After:** 1,788 lines (-14%)
- **Extracted:**
  - ConfigModule (140 lines) - modules/config/
  - AppModule (90 lines) - modules/app/
  - HTTPModule (170 lines) - modules/network/

---

# Runtime Refactoring - Remaining Work

## Current State

**Interpreter.cpp:** 3,551 lines (reduced from 10,674 = -67%)

**Structure:**
```
Interpreter.cpp
├── Lines 1-529:    Helper functions, class definition (~530 lines)
├── Lines 530-3216: Visitor methods (~2,686 lines)
└── Lines 3220-3551: InitializeStandardLibrary() (332 lines)
```

## Remaining Refactoring Tasks

### 1. Split Visitor Methods (HIGH PRIORITY)

**Target:** Split into `ExprEvaluator.cpp` and `StatementEvaluator.cpp`

**Expression Visitors (~1,300 lines):**
- visitBinaryExpression
- visitUnaryExpression
- visitUpdateExpression
- visitCallExpression
- visitMemberExpression
- visitLambdaExpression
- visitArrayLiteral
- visitObjectLiteral
- visitSetLiteral
- visitStringLiteral
- visitNumberLiteral
- visitIfExpression
- visitBlockExpression
- visitPipelineExpression
- visitShellCommandExpression
- visitBacktickExpression
- visitInterpolatedStringExpression
- visitAsyncExpression
- visitAwaitExpression
- ... (25+ expression visitors)

**Statement Visitors (~1,400 lines):**
- visitProgram
- visitLetDeclaration
- visitFunctionDeclaration
- visitReturnStatement
- visitIfStatement
- visitBlockStatement
- visitWhileStatement
- visitForStatement
- visitForEachStatement
- visitBreakStatement
- visitContinueStatement
- visitExpressionStatement
- visitHotkeyBinding
- visitConditionalHotkey
- visitSwitchStatement
- visitTryCatchStatement
- ... (50+ statement visitors)

**Result:**
```
Interpreter.cpp:    ~863 lines (orchestration only)
ExprEvaluator.cpp:  ~1,300 lines
StatementEvaluator.cpp: ~1,400 lines
```

---

### 2. Split SystemModule (MEDIUM PRIORITY)

**Current:** 2,081 lines (monolithic)

**Split into:**
```
modules/system/
├── SystemInfoModule.cpp    # system.os(), system.hostname(), system.cpu()
├── EnvModule.cpp           # system.env(), environment variables
├── ProcessInfoModule.cpp   # process listing, system stats
└── HTTPModule.cpp          # http.get(), http.post(), http.download()
```

**Note:** HTTPModule might be better as separate `modules/network/`

---

### 3. Extract InitializeStandardLibrary (LOW PRIORITY)

**Current:** 332 lines in Interpreter.cpp

**Extract to:** `modules/app/AppModule.cpp`

**Functions:**
- app.args - CLI arguments
- app.enableReload/disableReload/toggleReload/reload
- runOnce() - Execute once per session
- debug.showAST/stopOnError
- Various debug utilities

**Requires:** Creating ReloadManager class to manage state

---

### 4. Module Registration Pattern (MEDIUM PRIORITY)

**Current:** Manual registration in ModuleLoader.cpp

**Target:** Automatic registration via metadata

```cpp
// modules/ModuleRegistry.hpp
struct ModuleInfo {
    const char* name;
    void (*registerFn)(Environment&, HostContext&);
};

// In each module:
REGISTER_MODULE(window, registerWindowModule)

// ModuleLoader.cpp becomes:
void loadAllModules(Environment& env, HostContext& ctx) {
    for (const auto& module : ModuleRegistry::getAll()) {
        module.registerFn(env, ctx);
    }
}
```

---

### 5. Merge PhysicsModule into MathModule (LOW PRIORITY)

**Current:** Separate PhysicsModule with just constants

**Target:** Move constants into MathModule

```cpp
// MathModule
(*mathObj)["PI"] = HavelValue(3.14159265358979323846);
(*mathObj)["E"] = HavelValue(2.71828182845904523536);
(*mathObj)["PHI"] = HavelValue(1.61803398874989484820);
(*mathObj)["GRAVITY"] = HavelValue(9.80665);  // m/s²
```

---

## Target Final Structure

```
src/
├── havel-lang/
│   ├── runtime/
│   │   ├── Interpreter.cpp       # ~863 lines (orchestration)
│   │   ├── ExprEvaluator.cpp     # ~1,300 lines
│   │   ├── StatementEvaluator.cpp # ~1,400 lines
│   │   └── Environment.cpp
│   │
│   └── stdlib/
│       ├── MathModule.*          # (includes physics constants)
│       ├── StringModule.*
│       ├── ArrayModule.*
│       ├── TypeModule.*
│       ├── RegexModule.*
│       ├── FileModule.*
│       └── ProcessModule.*
│
└── modules/
    ├── window/
    ├── brightness/
    ├── audio/
    ├── screenshot/
    ├── clipboard/
    ├── automation/
    ├── launcher/
    ├── media/
    ├── help/
    ├── filesystem/
    ├── system/
    │   ├── SystemInfoModule.*
    │   ├── EnvModule.*
    │   └── ProcessInfoModule.*
    ├── gui/
    ├── alttab/
    ├── mapmanager/
    ├── io/
    ├── async/
    ├── timer/
    ├── network/                  # NEW - HTTP client
    └── app/                      # NEW - app.*, reload, debug
```

---

## Priority Order

1. **Split visitor methods** - Biggest impact (~2,700 lines → 3 files)
2. **Split SystemModule** - Prevents future monolith
3. **Module registration pattern** - Prevents giant init functions
4. **Extract InitializeStandardLibrary** - Cleanup
5. **Merge PhysicsModule** - Minor cleanup

---

## Benefits of Completing Refactoring

1. **Interpreter.cpp ~863 lines** - Easily understandable core
2. **Clear separation** - Expressions vs Statements vs Orchestration
3. **No monoliths** - Largest file ~1,400 lines
4. **Automatic registration** - No manual module listing
5. **Testable units** - Each evaluator can be tested independently

---

## Current Achievement

✅ **67% reduction achieved** (10,674 → 3,551 lines)
✅ **Clean architecture** - Pure stdlib vs host bindings
✅ **Embeddable** - havel-lang/ compiles without Qt/X11
✅ **Modular** - 18 feature modules
✅ **Documented** - This plan + RUNTIME_REFACTORING.md

**Remaining to 2k target:** ~1,551 lines (44% of current size)
