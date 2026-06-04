# Compiler Pipeline

Detailed documentation of the Havel compiler: from source text to bytecode.

---

## Pipeline Overview

```
Source (.hv)
    |
    v
+--------+
| Lexer  |  Tokenizes source into a Token stream
+--------+
    |
    v
+--------+
| Parser |  Precedence-climbing parser produces AST
+--------+
    |
    v
+------------------+
| Semantic Analyzer|  Type checking, symbol resolution, imports
+------------------+
    |
    v
+------------------+
| Bytecode Compiler|  IR generation, optimization, bytecode emission
+------------------+
    |
    v
+------------------+
| VM / JIT         |  Stack-based VM execution or LLVM JIT
+------------------+
```

Orchestrated by `Pipeline` (`src/havel-lang/compiler/core/Pipeline.cpp`):

```cpp
PipelineOptions opts;
opts.host_functions = bridge.collectFunctions();
opts.host_globals = bridge.collectGlobals();
auto result = Pipeline::compileAndRun(source, "script.hv", opts);
```

---

## Stage 1: Lexer

**Source**: `src/havel-lang/lexer/Lexer.cpp`, `Lexer.hpp`

### Responsibilities

- Tokenizes source text into a `Token` stream
- Handles string interpolation (`{var}` and `$var` inside strings)
- Recognizes hotkey modifier prefixes: `^` (Ctrl), `+` (Shift), `!` (Alt), `#` (Super)
- Context-aware hotkey scanning: `|`, `&`, `!=` at statement start route to `scanHotkey()` when followed by hotkey characters
- Tracks indentation for colon-block parsing

### Token Types

Key token categories:

| Category | Examples |
|----------|----------|
| Literals | `NUMBER`, `STRING`, `FSTRING`, `REGEX`, `CHAR` |
| Identifiers | `IDENTIFIER`, `keyword-as-identifier` |
| Keywords | `FN`, `IF`, `ELSE`, `WHILE`, `FOR`, `CLASS`, `STRUCT`, `TRAIT`, `PROT`, `IMPL`, `USE`, `MATCH`, `GO`, `CO` |
| Operators | `PLUS`, `MINUS`, `STAR`, `SLASH`, `ASSIGN`, `ARROW`, `PIPE`, `NULLISH_COALESCE` |
| Hotkey modifiers | `CARET` (Ctrl), `PLUS` (Shift), `BANG` (Alt), `HASH` (Super) |
| Delimiters | `LPAREN`, `RPAREN`, `LBRACE`, `RBRACE`, `LBRACKET`, `RBRACKET` |
| Special | `NEWLINE`, `INDENT`, `DEDENT`, `EOF` |

### Hotkey Lexing

The lexer uses context-aware scanning at statement start:

- `|` at statement start with a hotkey char next → `scanHotkey()` (passthrough prefix)
- `!=` at statement start with `=>` following → `scanHotkey()` (Alt+Equals)
- `&` at statement start → `scanHotkey()` (chord operator)
- `isHotkeyChar()` checks for `|`, `&`, `:`, `!`, `=` characters

This disambiguates hotkey syntax from bitwise operators and comparison operators.

### String Interpolation

Inside `"..."` strings:
- `{expression}` → bare brace interpolation
- `$variable` → short-form variable interpolation

Inside `f"..."` strings:
- `${expression}` → f-string interpolation
- `{expression}` → also works

Inside backtick strings:
- `{expression}` → shell command interpolation

### Safety Limits

- Maximum token count: 5,000,000 per parse
- Line length: unlimited (uses buffer growth)

---

## Stage 2: Parser

**Source**: `src/havel-lang/parser/Parser.cpp`, `Parser.h`

### Algorithm

Precedence-climbing (Pratt) parser. Each token type has a prefix and/or infix binding power. The parser recursively descends through operator precedence levels.

### Binding Powers (low to high)

| Level | Operators | Associativity |
|-------|-----------|---------------|
| 0 | Prefix unary | — |
| 10 | `=` `+=` `-=` `*=` `/=` `%=` | Right |
| 15 | `? :` ternary | Right |
| 20 | `??` nullish coalesce | Right |
| 25 | `or` | Left |
| 30 | `and` | Left |
| 35 | `\|>` `<|` pipe | Left |
| 50 | `is` `matches` | Left |
| 55 | `in` | Left |
| 60 | `==` `!=` `<` `>` `<=` `>=` | Left |
| 65 | `..` `..=` range | Right |
| 70 | `+` `-` | Left |
| 80 | `*` `/` `%` `//` `%%` | Left |
| 90 | `**` power | Right |
| 100 | Postfix `?` `++` `--` | — |
| 110 | `.` `.?` `?.` member access | Left |

### Key AST Node Types

#### Statements

| Node | Description |
|------|-------------|
| `Program` | Root node containing all statements |
| `LetDeclaration` | Mutable variable binding |
| `ConstDeclaration` / `ValDeclaration` | Immutable binding |
| `FunctionDeclaration` | Function with params, body, optional return type |
| `ClassDeclaration` | Class with instance/class fields and methods |
| `StructDeclaration` | Lightweight data container |
| `ProtocolDeclaration` | Protocol with required method signatures |
| `TraitDeclaration` | Trait with optional default method bodies |
| `ImplDeclaration` | Implementation of trait/protocol for a type |
| `EnumDeclaration` | Sum type with variants |
| `IfStatement` | Conditional with optional else chain |
| `WhileStatement` | While loop |
| `ForStatement` | For-in loop with destructuring support |
| `LoopStatement` | Infinite loop |
| `RepeatStatement` | Repeat N times |
| `MatchStatement` | Pattern matching |
| `HotkeyBinding` | `key => action` registration |
| `ConditionalHotkey` | `key if condition => action` |
| `WhenBlock` | `when condition { hotkeys... }` |
| `TryStatement` | Try/catch/finally |
| `UseStatement` | Module import |
| `GoStatement` | Goroutine spawn |
| `ShellCommandStatement` | `$ cmd` |
| `ConfigSection` | `name { key = value }` |

#### Expressions

| Node | Description |
|------|-------------|
| `Identifier` | Variable reference |
| `BinaryExpression` | Infix operator |
| `UnaryExpression` | Prefix/postfix operator |
| `CallExpression` | Function call with arguments |
| `MemberExpression` | `.field` access |
| `IndexExpression` | `[index]` access |
| `AssignmentExpression` | `name = value` |
| `ArrayExpression` | `[1, 2, 3]` |
| `ObjectExpression` | `{ key: value }` |
| `LambdaExpression` | `x => expr` or `fn(x) { body }` |
| `IfExpression` | `if cond { a } else { b }` as value |
| `AwaitExpression` | `await expr` or `<- expr` |
| `RangeExpression` | `1..10` or `1..=10` |
| `BacktickExpression` | `` `cmd` `` shell capture |
| `SliceExpression` | `arr[start:end:step]` |
| `SpreadExpression` | `...expr` |

### Hotkey Parsing

The parser uses lookahead for hotkey detection:

1. Identifier followed by `&` + identifier + `=>` → chord hotkey (`RShift & WheelUp`)
2. Identifier followed by `:` + modifier + `=>` → timing modifier (`numpad5:up`)
3. Lookahead scans for `=>` before committing to hotkey vs. expression statement

### Parser Guards

- `DepthGuard` — RAII guard checking recursion depth (max 512)
- `ProgressGuard` — RAII guard checking token count (max 5,000,000)

Both throw on limit exceeded to prevent stack overflow or infinite loops during parsing.

---

## Stage 3: Semantic Analyzer

**Source**: `src/havel-lang/semantic/`

### Responsibilities

- Type checking (annotations are parsed but not enforced at runtime currently)
- Symbol table management — resolves variable references to their declaration scope
- Module resolution — processes `use` and `import` statements
- Import validation — checks that imported names exist
- Protocol/trait conformance checking
- `TypeCheckResult` — output consumed by the bytecode compiler for optimization

---

## Stage 4: Bytecode Compiler

**Source**: `src/havel-lang/compiler/core/ByteCompiler.cpp`, `ByteCompiler.hpp`

### IR Generation

The compiler walks the typed AST and emits bytecode IR instructions into a `BytecodeChunk`. Each chunk contains:

- `instructions` — ordered list of `Instruction` structs (opcode + operands)
- `constants` — constant pool (numbers, strings)
- `function_entries` — nested function definitions
- `string_entries` — interned strings

### Function Index Reservation

Before emitting any code, the compiler makes a pass over top-level statements to reserve function indices:

1. Top-level functions → `function_indices_by_node_`
2. Class methods (instance + class) → indexed per class
3. Impl methods → `impl_method_nodes_` + `impl_method_type_names_`
4. Protocol/trait declarations → collected into `top_level_protocol_names_`
5. Lambda functions → `lambda_indices_by_node_`

### Known Name Seeding

For REPL persistence, the compiler accepts known sets:

```cpp
compiler.setKnownClassNames(repl.known_class_names_);
compiler.setKnownStructNames(repl.known_struct_names_);
compiler.setKnownProtocolNames(repl.known_protocol_names_);
compiler.setKnownImplNames(repl.known_impl_names_);
```

These seed the `top_level_*_names_` sets so the compiler does not re-declare types that already exist in the VM.

### Tail Call Optimization

The compiler tracks `in_tail_position_` to emit `TAIL_CALL` instead of `CALL` when the last expression in a function body is a call. Critical fix: `in_tail_position_` is saved/cleared/restored around assignment RHS compilation to prevent incorrect tail call optimization on non-tail positions.

### Key Emitted Patterns

| Havel Code | Bytecode Pattern |
|------------|-----------------|
| `x = 5` | `LOAD_CONST 5` → `STORE_VAR x_idx` |
| `fn foo() { ... }` | Reserve index → compile body → emit `DEFINE_FUNC` |
| `foo(args)` | `LOAD_VAR foo_idx` → `PUSH args` → `CALL` |
| `if cond { a } else { b }` | `JUMP_IF_FALSE else_ip` → `a` → `JUMP end_ip` → `b` → `end_ip:` |
| `for x in range` | `LOAD_VAR range` → `ITER_START` → body → `ITER_NEXT` → `JUMP start` |
| `F1 => { ... }` | Compile callback → `HOST_CALL hotkey.register` |

### Output

The compiler produces a `BytecodeChunk` ready for the VM to execute. The chunk is self-contained: all function definitions, constants, and string references are embedded.

---

## Stage 5: VM Execution

See [vm-internals.md](vm-internals.md) for the full VM documentation.

The VM interprets bytecode instructions via a dispatch loop (or compiles hot paths via LLVM JIT when enabled).

---

## Module Loading

### `use module`

1. `ModuleLoader::resolve()` finds the `.hv` file on search paths
2. The file is parsed and compiled independently
3. The module executes in a sandboxed global scope
4. An exports object is returned with all top-level definitions
5. The caller accesses exports via `module.function()` or destructured imports

### `load("file.hv")`

1. Resolves file path via `ModuleLoader::resolve()` with direct filesystem fallback
2. Parses and compiles the file
3. Registers protocol/impl info from AST with the VM
4. Executes in the **caller's** global scope (not sandboxed)
5. Wraps new `FunctionObjId` globals into `RuntimeClosure` objects
6. Merges all new definitions into the caller's globals
7. Returns `true` on success

Key difference: `load()` merges definitions into the current scope, while `use` returns an isolated exports object.

---

## Error Reporting

**Source**: `src/havel-lang/errors/`

Errors carry source location information:

```cpp
struct HavelError {
    std::string message;
    uint32_t line;
    uint32_t column;
    uint32_t length;
    std::string sourceLine;
    std::string filePath;
};
```

Errors are reported at each pipeline stage:

| Stage | Error Type | Example |
|-------|------------|---------|
| Lexer | `LexError` | Invalid character, unterminated string |
| Parser | `ParseError` | Unexpected token, missing closing brace |
| Semantic | `TypeError` | Type mismatch, undeclared variable |
| Compiler | `CompileError` | Duplicate definition, invalid break target |
| Runtime | `RuntimeError` | Division by zero, null dereference |

---

## Key Files

| File | Role |
|------|------|
| `src/havel-lang/lexer/Lexer.cpp` | Tokenizer (~2,145 lines) |
| `src/havel-lang/parser/Parser.cpp` | Parser (~11,022 lines) |
| `src/havel-lang/compiler/core/ByteCompiler.cpp` | Bytecode compiler (~7,867 lines) |
| `src/havel-lang/compiler/core/ByteCompiler.hpp` | Compiler interface |
| `src/havel-lang/compiler/core/BytecodeIR.hpp` | IR and chunk data structures |
| `src/havel-lang/compiler/core/Pipeline.cpp` | Pipeline orchestration |
| `src/havel-lang/compiler/core/CompilerUtils.cpp` | Shared compiler utilities |
| `src/havel-lang/semantic/` | Type checker and symbol resolution |
| `src/havel-lang/errors/` | Error reporting infrastructure |
