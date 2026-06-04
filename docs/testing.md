# Testing

How to test the Havel language implementation — C++ unit tests, Havel script tests, and the bytecode smoke test.

---

## Quick Start

```bash
./build.sh test              # Run all tests (build + test)
./build-debug/hvtest         # Run test suite after building
./build-debug/havel run scripts/test_basic.hv   # Run a specific script
```

---

## Test Categories

### 1. C++ Unit Tests

**Framework**: Google Test (gtest)
**Location**: `tests/`
**Build**: Enabled when `ENABLE_TESTS=ON` (default)

These test individual C++ components: lexer, parser, VM, GC, etc.

```bash
# Run all C++ tests
cd build-debug && ctest --output-on-failure

# Run a specific test
cd build-debug && ./havel-tests --gtest_filter=LexerTest.*

# Run with verbose output
cd build-debug && ./havel-tests --gtest_print_time=0 --gtest_output=xml:results.xml
```

### 2. Havel Script Tests

**Location**: `scripts/tests/*.hv`
**Run via**: `./build-debug/havel run <script>`

These are Havel scripts that exercise language features. They use `assert()` for verification:

```bash
# Run a specific test script
./build-debug/havel run scripts/tests/test_basic.hv

# Run with full debugging
./build-debug/havel run scripts/tests/test.hv -d -dl -dp -da --debug-bytecode --debug-jit
```

### 3. Bytecode Smoke Test

**Binary**: `havel-bytecode-smoke` (Debug builds only)
**Why Debug only**: Release LTO causes relocation overflow — known limitation

```bash
./build-debug/havel-bytecode-smoke
```

This validates bytecode generation and basic VM execution without the full application stack.

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
// test_my_feature.hv

// Basic assertion
assert(1 + 1 == 2)

// With message
assert(10 > 5, "basic comparison failed")

// Test function definitions
fn add(a, b) { a + b }
assert(add(3, 4) == 7)

// Test classes
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
