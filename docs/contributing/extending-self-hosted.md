---
title: "Extending Self-Hosted Compiler"
description: "How to modify the Havel-written compiler pipeline in modules/lang/."
---

# Extending Self-Hosted Compiler

## Overview

The self-hosted compiler lives in `modules/lang/` and is written in Havel itself. It's the default pipeline (`--self-hosted`).

```
modules/lang/
├── lexer.hv        # Lexical analyzer
├── parser.hv       # Parser (Pratt/precedence climbing)
├── ast.hv          # AST node definitions
├── semantic.hv     # Type checking, symbol resolution
├── bytecode.hv     # Bytecode IR generation
├── optimizer.hv    # Optimization passes
├── emitter.hv      # Bytecode emission
└── pipeline.hv     # Orchestration
```

---

## Build Process

```bash
# After modifying .hv files
./emit_pipeline.sh ./build-debug/havel
```

This compiles all `modules/lang/*.hv` to `out/modules/lang/*.hvc`.

### Incremental Build

```bash
# Rebuild single module
./build-debug/havel --build modules/lang/parser.hv -o out/modules/lang/parser.hvc
```

---

## Adding a Language Feature

### 1. Lexer (tokens)

Add token type in `lexer.hv`:

```hv
// modules/lang/lexer.hv
enum TokenType {
    // ... existing ...
    MY_NEW_TOKEN
}

fn scanMyNewToken() {
    // implementation
}
```

### 2. Parser (AST)

Add AST node in `ast.hv`:

```hv
// modules/lang/ast.hv
class MyNewExpr : Expr {
    fn init(token, value) {
        @token = token
        @value = value
    }
}
```

Add parsing rule in `parser.hv`:

```hv
// modules/lang/parser.hv
fn parseMyNewExpr() {
    // precedence climbing integration
}
```

### 3. Semantic Analysis

Add type checking in `semantic.hv`:

```hv
// modules/lang/semantic.hv
fn visitMyNewExpr(expr) {
    // type check, symbol resolution
}
```

### 4. Bytecode Generation

Add IR emission in `bytecode.hv`:

```hv
// modules/lang/bytecode.hv
fn emitMyNewExpr(expr) {
    // emit bytecode instructions
    bc.emit(OpCode.LOAD_CONST, value)
    // ...
}
```

### 5. Optimization (Optional)

Add pass in `optimizer.hv`:

```hv
// modules/lang/optimizer.hv
fn optimizeMyNewExpr(expr) {
    // constant folding, dead code elimination
}
```

---

## Testing Changes

### 1. Rebuild Pipeline

```bash
./emit_pipeline.sh ./build-debug/havel
```

### 2. Test with Self-Hosted

```bash
./build-debug/havel test_feature.hv
```

### 3. Compare with Bootstrap

```bash
./build-debug/havel --self-hosted test_feature.hv > out1.txt
./build-debug/havel --no-self-hosted test_feature.hv > out2.txt
diff out1.txt out2.txt
```

### 4. Run Test Suite

```bash
./build-debug/hvtest --scripts
./build-debug/hvtest --smoke
```

---

## Debugging Self-Hosted Compiler

### Enable Debug Output

```bash
./build-debug/havel -dl -dp -da --debug-bytecode test.hv
```

- `-dl`: Lexer debug
- `-dp`: Parser debug (AST dump)
- `-da`: AST debug
- `--debug-bytecode`: Bytecode disassembly

### REPL Debugging

```bash
./build-debug/havel --repl
havel> .ast 1 + 2
havel> .bytecode myFunction
```

---

## Common Patterns

### Adding Keyword

```hv
// lexer.hv
val MY_KEYWORD = "mykeyword"

fn scanKeyword() {
    if match("mykeyword") {
        return Token(MY_KEYWORD, "mykeyword")
    }
    // ...
}
```

```hv
// parser.hv
fn parseStatement() {
    if match(MY_KEYWORD) {
        return parseMyKeywordStatement()
    }
    // ...
}
```

### Adding Operator

```hv
// lexer.hv
val MY_OP = "myop"
val MY_OP_PRECEDENCE = 75  // between + and *

// parser.hv - in precedence table
binding_power[MY_OP] = MY_OP_PRECEDENCE

fn parseMyOpExpr(left) {
    // infix parsing
}
```

### Adding Literal

```hv
// lexer.hv
fn scanMyLiteral() {
    // parse and return Token(MY_LITERAL, value)
}
```

```hv
// ast.hv
class MyLiteral : Expr { ... }
```

```hv
// parser.hv
fn parsePrimary() {
    if match(MY_LITERAL) {
        return MyLiteral(token.value)
    }
    // ...
}
```

---

## Pipeline Orchestration

`pipeline.hv` coordinates all stages:

```hv
// modules/lang/pipeline.hv
fn compile(source, filename, options) {
    // 1. Lex
    tokens = lexer.tokenize(source, filename)
    
    // 2. Parse
    ast = parser.parse(tokens)
    
    // 3. Semantic
    typed_ast = semantic.analyze(ast)
    
    // 4. Optimize
    optimized = optimizer.optimize(typed_ast)
    
    // 5. Emit bytecode
    bytecode = emitter.emit(optimized)
    
    return bytecode
}
```

---

## Bootstrap Synchronization

When changing self-hosted compiler, keep bootstrap (C++) in sync:

1. **C++ Lexer**: `src/havel-lang/lexer/Lexer.cpp`
2. **C++ Parser**: `src/havel-lang/parser/Parser.cpp`
3. **C++ Bytecode Compiler**: `src/havel-lang/compiler/core/ByteCompiler.cpp`

The bootstrap is used when:
- `--no-self-hosted` flag
- Self-hosted pipeline fails to load
- Bootstrapping new platforms

---

## Performance Considerations

- Self-hosted compiler runs on VM (interpreted or JIT)
- Bootstrap compiler is native C++
- For development: self-hosted rebuild ~2s vs C++ ~30s
- For release: both produce same bytecode

---

## Debugging Tips

### Print from Compiler

```hv
// In any compiler module
fn debug(msg) {
    if getGlobal("__compiler_debug") {
        print("[COMPILER] " + msg)
    }
}
```

```bash
# Enable
./build-debug/havel -E '__compiler_debug = true' test.hv
```

### Inspect Intermediate Representations

```hv
// In pipeline.hv
fn compile(source, filename, options) {
    tokens = lexer.tokenize(source, filename)
    if options.debug_lexer { print(tokens) }
    
    ast = parser.parse(tokens)
    if options.debug_ast { print(ast) }
    
    // ...
}
```

---

**Previous:** [Adding Host Functions](/contributing/adding-host-functions)
**Next:** [Changelog →](/changelog)