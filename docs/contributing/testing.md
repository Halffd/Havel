---
title: "Testing"
description: "Test suite: C++ unit tests, script tests, bytecode smoke tests, and scheduler rig."
---

# Testing

## Test Types

| Test Type | Location | Run Command |
|-----------|----------|-------------|
| C++ Unit Tests | `tests/` | `./build.sh test` |
| Script Tests | `scripts/tests/` | `./build-debug/hvtest --scripts` |
| Bytecode Smoke | `scripts/smoke/` | `./build-debug/hvtest --smoke` |
| Scheduler Rig | `scripts/tests/scheduler/` | `./build-debug/hvtest --scheduler` |

---

## Running Tests

### All Tests (via build system)

```bash
./build.sh test
```

Runs C++ unit tests via ctest.

### Script Tests

```bash
# All script tests
./build-debug/hvtest --scripts

# Single test
./build-debug/havel scripts/tests/test_basic.hv
```

### Bytecode Smoke Tests

```bash
./build-debug/hvtest --smoke
```

Verifies bytecode round-trip: compile → serialize → deserialize → execute.

### Scheduler Rig (124 tests)

```bash
./build-debug/hvtest --scheduler
```

Tests concurrency primitives: goroutines, channels, timers, waitgroups, hotkeys.

---

## Test Structure

### C++ Unit Tests (Google Test)

```cpp
// tests/test_vm.cpp
#include <gtest/gtest.h>
#include "havel-lang/compiler/vm/VM.hpp"

TEST(VMTest, BasicArithmetic) {
    havel::compiler::VM vm;
    auto result = vm.runString("1 + 2 * 3");
    EXPECT_EQ(result.asInt(), 7);
}
```

Build with `ENABLE_TESTS=ON` (modes 0, 4, 5, 6, 9, 12, 15).

### Script Tests

Each `.hv` file in `scripts/tests/` is a test. Should:
- Exit with code 0 on success
- Print test name and result
- Use assertions

```hv
// scripts/tests/test_math.hv
print("Testing math module...")

assert(math.abs(-5) == 5)
assert(math.sqrt(16) == 4)
assert(math.pow(2, 3) == 8)
assert(math.sin(math.pi / 2) == 1)

print("math tests passed")
```

### Smoke Tests

```hv
// scripts/smoke/test_basic.hv
fn test() { 42 }
assert(test() == 42)

fn add(a, b) => a + b
assert(add(1, 2) == 3)

print("smoke test passed")
```

---

## Writing Tests

### Test Naming

```
test_<feature>.hv          # Feature test
test_issue_<number>.hv     # Regression test
test_<feature>_<case>.hv   # Specific case
```

### Assertions

```hv
// Built-in assert (throws on failure)
assert(condition)
assert(condition, "message")

// Custom test helpers
fn assertEq(actual, expected) {
    if actual != expected {
        throw "assertEq failed: {actual} != {expected}"
    }
}

fn assertThrows(fn) {
    try { fn(); throw "expected throw" } catch { }
}
```

### Test Output

```
Testing math module...
math tests passed
[OK] test_math.hv
```

---

## CI Pipeline

The CI runs this sequence:

1. **CMake configure** — detect dependencies, generate build files
2. **Build** — compile all targets
3. **Bytecode smoke** — `havel-bytecode-smoke`
4. **CTest** — run all gtest-based unit tests

```bash
# Replicate CI locally
./build.sh 0 build && \
./build-debug/havel-bytecode-smoke && \
cd build-debug && ctest --output-on-failure
```

---

## Test File Organization

### C++ Tests

```
tests/
├── CMakeLists.txt          # Test target definitions
├── TestMain.cpp            # Gtest main entry point
├── LexerTest.cpp           # Lexer unit tests
├── ParserTest.cpp          # Parser unit tests
├── VMTest.cpp              # VM unit tests
├── GCTest.cpp              # Garbage collector tests
└── ...                     # Other component tests
```

### Havel Script Tests

```
scripts/
├── test.hv                 # Main test script
├── test_basic.hv           # Basic language features
└── tests/
    ├── test_arithmetic.hv  # Arithmetic operations
    ├── test_strings.hv     # String operations
    ├── test_arrays.hv      # Array operations
    ├── test_objects.hv     # Object operations
    ├── test_functions.hv   # Function definitions and calls
    ├── test_classes.hv     # Class/struct features
    ├── test_channels.hv    # Channel operations
    ├── test_concurrency.hv # Goroutine and thread tests
    └── ...                 # Other feature tests
```

---

## Writing C++ Tests

### Pattern

```cpp
#include <gtest/gtest.h>
#include "havel-lang/lexer/Lexer.hpp"
#include "havel-lang/compiler/core/Pipeline.hpp"

TEST(LexerTest, TokenizesIntegers) {
    havel::compiler::Pipeline pipeline;
    auto tokens = pipeline.tokenize("42");
    ASSERT_EQ(tokens.size(), 2);  // INT + EOF
    EXPECT_EQ(tokens[0].type, TokenType::INT);
    EXPECT_EQ(tokens[0].value, "42");
}

TEST(VMTest, ExecutesArithmetic) {
    havel::compiler::VM vm;
    havel::compiler::registerStdLibModules(vm);
    auto result = vm.runString("2 + 3");
    EXPECT_TRUE(result.isInt());
    EXPECT_EQ(result.asInt(), 5);
}
```

### Adding to CMakeLists.txt

```cmake
add_executable(havel-tests
    tests/TestMain.cpp
    tests/LexerTest.cpp
    tests/MyNewTest.cpp
)
target_link_libraries(havel-tests PRIVATE havel-lang GTest::GTest)
add_test(NAME MyNewTest COMMAND havel-tests --gtest_filter=MyNewTest*)
```

---

## Writing Havel Script Tests

### Pattern

Havel test scripts use `assert()` for validation:

```
# test_my_feature.hv

# Basic assertion
assert(1 + 1 == 2)

# With message
assert(10 > 5, "basic comparison failed")

# Test function definitions
fn add(a, b) { a + b }
assert(add(3, 4) == 7)

# Test classes
class Point :
    x = 0
    y = 0

    fn new(x, y) :
        @x = x
        @y = y

p = Point(3, 4)
assert(p.x == 3)
assert(p.y == 4)
```

### Naming Convention

- `test_<feature>.hv` — e.g., `test_strings.hv`, `test_channels.hv`
- Place in `scripts/tests/`

---

## Debugging Test Failures

### Verbose VM Execution

```bash
./build-debug/havel run failing_test.hv -d -dl -dp -da --debug-bytecode
```

| Flag | What it shows |
|------|---------------|
| `-d` | General debug output |
| `-dl` | Lexer tokens |
| `-dp` | Parser AST |
| `-da` | AST analysis |
| `--debug-bytecode` | Bytecode execution trace |
| `--debug-jit` | JIT compilation trace |

### GDB

```bash
./build.sh 6 build
gdb --args ./build-debug/havel run failing_test.hv
(gdb) break handleScriptThrow
(gdb) run
(gdb) bt
```

### Tmux Debugging

For long-running or interactive debugging:

```bash
tmux new-session -s debug
./build-debug/havel run failing_test.hv -d -dbc -da -dp -dl
```

### ASAN Errors

Debug builds have ASAN enabled. If you see an AddressSanitizer error:

1. Look for the stack trace in the error output
2. The error report shows the exact memory operation that caused the issue
3. Use the stack trace to find the source location

---

## Test Coverage Areas

| Component | C++ Tests | Script Tests |
|-----------|-----------|--------------|
| Lexer | LexerTest | — |
| Parser | ParserTest | — |
| Semantic Analyzer | SemanticTest | — |
| ByteCompiler | CompilerTest | — |
| VM Operations | VMTest | test_basic.hv, test_arithmetic.hv |
| GC | GCTest | — |
| Strings | — | test_strings.hv |
| Arrays | — | test_arrays.hv |
| Objects | — | test_objects.hv |
| Functions | — | test_functions.hv |
| Classes/Structs | — | test_classes.hv |
| Concurrency | — | test_concurrency.hv, test_channels.hv |
| Host Functions | HostFnTest | — |
| Module Loading | ModuleTest | — |
| Error Handling | — | test_errors.hv |
| Hotkeys | — | test_hotkeys.hv |
| REPL | REPLTest | — |

---

## Brightness Hardware Test

**NOT in ctest. Requires interactive confirmation.**

```bash
./build-debug/brightness_test
```

- Applies real monitor changes
- Restores state on exit
- **Never run headless/SSH**

---

**Previous:** [Directory Structure](/contributing/directory-structure)
**Next:** [Code Style →](/contributing/code-style)