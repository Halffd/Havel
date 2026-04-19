# AGENTS.md

## Build System

Primary build: `./build.sh [mode] [command]`

| Mode | Type | Tests | Havel Lang | LLVM | Build Dir |
|------|------|-------|------------|------|-----------|
| 0 | Debug | ✓ | ✓ | ✓ | build-debug |
| 5 | Release | ✓ | ✓ | ✓ | build-release |
| 6 | Debug | ✓ | ✓ | ✗ | build-debug (default) |
| 9 | Release | ✓ | ✓ | ✗ | build-release |

Common commands:
- `./build.sh 5 build` - Full release with LLVM
- `./build.sh 6 build` - Fast debug (no LLVM)
- `./build.sh test` - Run all tests
- `./build.sh detect` - Show system dependencies

## Executables

| Binary | Purpose |
|--------|---------|
| `build-debug/havel` | Main application |
| `build-debug/havel-lsp` | Language Server Protocol |
| `build-debug/havel-bytecode-smoke` | Bytecode smoke test (Debug only) |

Run Havel scripts: `./build-debug/havel script.hv`

## Architecture Notes

- **Havel Language** (`src/havel-lang/`): Compiler pipeline (Lexer → Parser → Semantic → Bytecode) + VM + optional LLVM JIT
- **Host Modules** (`src/havel-lang/stdlib/`): Provide built-in functions (Math, String, Array, Hotkey, Window, etc.)
- **UI**: Qt6 loaded as dynamic extension, not linked to main binary
- **Wayland**: Protocols auto-generated at build time to `build/generated/wayland/`

## Testing

- **C++ unit tests**: `tests/` directory, gtest-based, built when ENABLE_TESTS=ON
- **Havel script tests**: `scripts/*.hv` files
- **Bytecode smoke test**: `havel-bytecode-smoke` (Debug builds only - Release LTO causes relocation overflow)

CI runs: CMake configure → build → bytecode-smoke → ctest

## Dependencies

Critical (fatal if missing):
- X11, Xtst, Xrandr, Xinerama, Xcomposite
- spdlog, nlohmann_json (pkg-config)
- Lua 5.4
- Tesseract OCR, Leptonica, OpenCV

Optional (graceful fallback):
- LLVM (JIT compilation)
- Qt6, GTK4, ImGui+GLFW (UI backends)
- PipeWire, PulseAudio, ALSA (audio)

## Build Quirks

- **Compiler**: Clang is forced (`CC=clang CXX=clang++`)
- **C++ Standard**: C++23
- **Debug builds**: ASAN + UBSAN enabled automatically
- **Release builds**: Thin LTO + native march + visibility hidden
- **LLVM ↔ Havel Lang**: LLVM JIT requires ENABLE_HAVEL_LANG (auto-enabled if missing)
- **Qt6/MOC conflicts**: `#define True=1`, `#define False=0`, etc. to fix keyword conflicts

## Bytecode Optimizations

### Jump Threading

Replacing a jump to another jump with a direct jump to the final target.

**Before optimization:**
```asm
L1: JUMP L2
L2: JUMP L3
L3: ADD ...
```

**After optimization:**
```asm
L1: JUMP L3
L2: JUMP L3  ; unreachable, can be removed
L3: ADD ...
```

**Bytecode example:**
```javascript
// Before
0: JUMP 2
1: ADD 1, 2     ; dead code
2: JUMP 4
3: MUL 2, 3     ; dead code
4: RETURN

// After jump threading
0: JUMP 4
2: JUMP 4
4: RETURN

// After dead code elimination
0: JUMP 4
4: RETURN
```

**Implementation in ByteCompiler.cpp:**
```cpp
void ByteCompiler::optimizeJumps() {
    // First pass: build jump targets
    std::unordered_map<uint32_t, uint32_t> jump_targets;
    for (size_t i = 0; i < instructions.size(); i++) {
        if (instr.opcode == OpCode::JUMP ||
            instr.opcode == OpCode::JUMP_IF_FALSE ||
            instr.opcode == OpCode::JUMP_IF_TRUE) {
            uint32_t target = instr.operands[0].asInt();
            jump_targets[i] = target;
        }
    }

    // Second pass: follow chains
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& [pc, target] : jump_targets) {
            while (instructions[target].opcode == OpCode::JUMP) {
                target = instructions[target].operands[0].asInt();
                jump_targets[pc] = target;
                changed = true;
            }
        }
    }

    // Third pass: patch instructions
    for (auto& [pc, target] : jump_targets) {
        instructions[pc].operands[0] = target;
    }
}
```

### INCLOCAL (Increment Local Variable)

`++x` or `x++` as a single opcode.

**Before:**
```asm
LOAD_VAR 0     ; push x
LOAD_CONST 1   ; push 1
ADD            ; x + 1
STORE_VAR 0    ; x = x + 1
```

**After (prefix ++x):**
```asm
INCLOCAL 0     ; increments slot 0, pushes result
```

**After (postfix x++):**
```asm
INCLOCAL_POST 0  ; increments slot 0, pushes old value
```

**Implementation BytecodeIR.hpp:**
```cpp
enum class OpCode : uint8_t {
    // ... existing ...
    INCLOCAL,       // ++local (prefix)
    DECLOCAL,       // --local (prefix)
    INCLOCAL_POST,  // local++ (postfix)
    DECLOCAL_POST,  // local-- (postfix)
};
```

**VM.cpp:**
```cpp
case OpCode::INCLOCAL: {
    uint32_t slot = instruction.operands[0].asInt();
    Value old = locals[slot];
    if (old.isInt()) {
        int64_t new_val = old.asInt() + 1;
        locals[slot] = Value::makeInt(new_val);
        pushStack(locals[slot]);
    } else if (old.isDouble()) {
        double new_val = old.asDouble() + 1.0;
        locals[slot] = Value::makeDouble(new_val);
        pushStack(locals[slot]);
    } else {
        COMPILER_THROW("Cannot increment non-numeric value");
    }
    break;
}

case OpCode::INCLOCAL_POST: {
    uint32_t slot = instruction.operands[0].asInt();
    Value old = locals[slot];
    pushStack(old);  // push old value first
    if (old.isInt()) {
        locals[slot] = Value::makeInt(old.asInt() + 1);
    } else if (old.isDouble()) {
        locals[slot] = Value::makeDouble(old.asDouble() + 1.0);
    } else {
        COMPILER_THROW("Cannot increment non-numeric value");
    }
    break;
}
```

**ByteCompiler.cpp:**
```cpp
void ByteCompiler::compileUnaryExpression(const ast::UnaryExpression& expr) {
    if (expr.op == ast::UnaryOperator::Increment) {
        auto* ident = dynamic_cast<ast::Identifier*>(expr.operand.get());
        if (!ident) {
            COMPILER_THROW("Increment requires identifier");
        }
        auto binding = bindingFor(*ident);
        if (binding->kind == ResolvedBindingKind::Local) {
            if (expr.isPostfix) {
                emit(OpCode::INCLOCAL_POST, binding->slot);
            } else {
                emit(OpCode::INCLOCAL, binding->slot);
            }
        } else {
            COMPILER_THROW("Can only increment local variables");
        }
        return;
    }
    // ... other unary ops
}
```

### Combined Optimization

```javascript
// Source: for i in 0..1000 { sum += i }

// Without INCLOCAL
LOAD_VAR i
LOAD_CONST 1
ADD
STORE_VAR i

// With INCLOCAL
INCLOCAL i
```

With jump threading, loops become tighter.

### Performance Impact

| Optimization       | Instructions saved | Speedup  |
|--------------------|--------------------|----------|
| Jump threading     | 2-3 per jump chain | 5-10%    |
| INCLOCAL           | 3 instructions → 1 | 10-15%   |
| Combined           | Significant for loops | 20-30% |

### For JIT Compilation

These optimizations are even more important for JIT because:
- Fewer instructions = less LLVM IR = faster compilation
- Simpler patterns = better optimization opportunities
