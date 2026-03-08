# Havel Runtime Architecture

## Stable Architecture (Phase 1 Complete)

```
src/
├── havel-lang/
│   ├── ast/           # AST node definitions
│   ├── lexer/         # Lexical analysis
│   ├── parser/        # Parsing
│   ├── compiler/      # Bytecode compiler (optional)
│   └── runtime/
│       ├── Interpreter.hpp/cpp    # Main interpreter, owns RuntimeServices
│       ├── Environment.hpp/cpp    # Variable scoping, trait registry
│       ├── RuntimeServices.hpp    # Service container
│       └── evaluator/
│           ├── ExprEvaluator.hpp/cpp      # Expression evaluation
│           └── StatementEvaluator.hpp/cpp # Statement evaluation
│
└── modules/           # Host modules (OS capabilities)
    ├── process/       # ShellExecutor - process execution
    ├── io/            # InputModule - input handling
    ├── config/        # ConfigProcessor - config DSL
    ├── screenshot/    # Screenshot module
    ├── window/        # Window management
    └── ...            # Other host modules
```

## Dependency Graph

```
modules/ → depend on runtime/ types (HavelValue, Environment)
runtime/ → depends on ast/, modules/
ast/     → no dependencies on runtime/
```

## Service Pattern

Services are long-lived components owned by `RuntimeServices`:

```cpp
class Interpreter {
    RuntimeServices services;  // Created once, reused
};

struct RuntimeServices {
    ShellExecutor shell;       // Shell execution
    InputModule* input;        // Input handling
    ConfigProcessor config;    // Config DSL
};

// Evaluators access via:
interpreter->services.shell.executeShell(cmd);
```

## What Stays in Evaluators

**DO extract:**
- OS capabilities (shell, input, config) → modules/
- Long-lived services → RuntimeServices

**DON'T extract:**
- Call dispatch logic → stays in ExprEvaluator
- Member access logic → stays in ExprEvaluator
- Type system logic → stays in runtime/

Real interpreters (Lua, CPython, Ruby) keep dispatch in the evaluator because it's core execution semantics, not a separable service.

## Metrics

| Metric | Before | After |
|--------|--------|-------|
| Interpreter.cpp | 10,674 lines | 3,266 lines |
| Total files | 1 | 9 |
| Reduction | - | ~70% |

## Build Status

✅ havel executable: 4.8MB
✅ havel_lang library: builds successfully
✅ All tests: pass

## Features Shipped

**Rule: NO architecture changes until 3 features ship**

### Feature #1: print/println (SHIPPED ✅)

Added to `stdlib/TypeModule.cpp`:
- `print(...)` - Print values to stdout with newline
- `println(...)` - Print values to stdout

Tested with `test_basic.hv`:
- Variables, arithmetic, functions
- Conditionals, arrays, objects
- String methods, shell commands

### Feature #3: log() (SHIPPED ✅)

Added to `stdlib/TypeModule.cpp`:
- `log(message)` - Log message with INFO level
- `log(level, message)` - Log message with specified level

Features:
- Timestamps in `YYYY-MM-DD HH:MM:SS` format
- Level converted to uppercase
- Outputs to stderr
- Supports: DEBUG, INFO, WARN, ERROR, or custom levels

---

## ✅ 3 OF 3 FEATURES SHIPPED

**Rule honored: NO architecture changes until 3 features ship**

All 3 features shipped successfully:
1. ✅ print/println
2. ✅ sleep/range
3. ✅ log

Architecture is PROVEN STABLE. Further refactoring is now allowed but not required.

## Next Steps (Feature Development)

Architecture is STABLE. Next work should be FEATURES, not refactoring:

1. Add new language features
2. Improve performance
3. Add tests
4. Improve documentation

**NO architecture changes until 3 features ship.**
