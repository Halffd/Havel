# Spec: type-system-design

Scope: feature

# Type System Feature Spec

## Overview
Add a compile-time TypeChecker pass to the Havel compiler pipeline that verifies protocol conformance, validates type hints, and enables type-aware optimizations — making Havel's type system Swift/TypeScript-like.

## Design Decisions

### 1. Type Representation

```
// In TypeChecker.hpp
enum class TypeKind {
    Builtin,     // int, number, string, bool, array, object, function, null
    Nominal,     // user-defined: class Foo, struct Bar
    Protocol,    // prot Drawable
    Union,       // int | string (future, not in v1)
    Dynamic,     // any/unknown — no compile-time check
};

struct TypeInfo {
    TypeKind kind;
    std::string name;           // "int", "Circle", "Drawable"
    std::vector<std::string> methodNames;  // for nominal/protocol types
    std::unordered_map<std::string, FunctionSignature> methods;  // name -> signature
};

struct FunctionSignature {
    std::vector<std::string> paramTypes;  // "" = untyped
    std::string returnType;               // "" = untyped
    size_t arity;
};
```

### 2. TypeRegistry (first-pass collection)

The TypeChecker first pass walks the entire AST and builds a TypeRegistry:
- For each `ProtocolDeclaration`: register protocol name + required method signatures
- For each `StructDeclaration`: register struct name + field names + method signatures + declared protocols
- For each `ClassDeclaration`: register class name + method signatures + declared protocols
- For each `FunctionDeclaration` at top level: register function signature
- For each `ImplDeclaration`: link type to protocol (validate later)
- Builtin types (int, number, string, bool, array, object, function) pre-registered

### 3. Protocol Contract Verification

When a type declares `: ProtocolName`:
1. Look up ProtocolName in TypeRegistry — error if not found: `"protocol 'Drawable' is not defined"`
2. For each required method in the protocol:
   - Check the type has a method with the same name — error: `"type 'Circle' does not implement required method 'draw' from protocol 'Drawable'"`
   - If the method exists, check parameter count matches — error: `"method 'draw' on 'Circle' has 2 parameters but protocol 'Drawable' requires 0"`
   - If both method and protocol have type annotations, check they are compatible — warning: `"method 'draw' on 'Circle' returns 'int' but protocol 'Drawable' declares 'string'"`
3. If a required method is missing AND the protocol has a defaultBody, mark the method for auto-injection

**Structural verification**: Even without `: ProtocolName`, if code calls `x as Drawable` or `x is Drawable`, we verify at the call site that the value's type (if known) could implement the protocol. This is the TypeScript-like part — structural subtyping.

### 4. Type Hint Enforcement

Current behavior: `fn foo(x: int)` → runtime `assert(type(x) == "int")`
New behavior:
- If `x: int` and x is a local variable with known type, TypeChecker validates assignments
- If `x: Circle` (nominal type), normalizeTypeAnnotation now returns the type name instead of nullopt
- Runtime assertions still emitted as fallback for dynamic values (function params, globals)
- Compile-time errors for obviously wrong assignments: `let y: int = "hello"` → error

**For custom/nominal type hints**: 
- `fn foo(x: Circle)` — normalizeTypeAnnotation returns "Circle" 
- ByteCompiler emits runtime check: `assert(type(x) == "struct" && x.__name == "Circle")` 
  or more precisely: uses `PROT_CHECK` with "Circle" if Circle is in type registry

### 5. Type Narrowing

Track type information through control flow:

```
x = something()
if x is Drawable {
    // TypeChecker knows: x is Drawable inside this block
    x.draw()  // verified — Drawable requires draw()
}

result = x as Drawable
if result != null {
    // TypeChecker knows: result is Drawable
    result.draw()  // verified
}
```

Implementation: TypeChecker maintains a `TypeEnv` (stack of scopes mapping variable names → known types). When entering an `if x is Drawable` block, push a narrowed type for x. On block exit, pop.

### 6. Protocol Default Methods

If `prot Drawable { fn draw() { print("default") } }` and `struct Circle : Drawable { }` (no draw method):
- TypeChecker sees Circle is missing `draw` but Drawable has a defaultBody
- Mark `draw` for injection into Circle
- ByteCompiler: when compiling StructDeclaration, emit additional `struct.method(Circle, draw, <default_closure>)` calls for injected methods

### 7. ByteCompiler Optimizations

With TypeChecker results available:
- **Skip PROT_CHECK when statically known**: If TypeChecker proved `c is Drawable` is always true, emit no opcode (just push true)
- **Skip PROT_CAST**: If value is statically known to conform, skip the cast check and just pass through
- **Direct method dispatch**: If type is known and method is known, emit CALL_METHOD directly instead of dynamic lookup
- **Type-specialized arithmetic**: If both operands are known `int`, skip number type checks

### 8. Error Reporting

All type errors go through the existing ErrorReporter:
```cpp
ErrorReporter::instance().errorAt(ErrorStage::Semantic, message, line, column);
```

Error categories:
- **Error**: Protocol contract violation, undefined type, incompatible assignment
- **Warning**: Type annotation mismatch (method return type differs from protocol), unused type hint
- **Info**: Type narrowing applied, default method injected

### 9. Pipeline Integration

```
Source → Lexer → Parser → LexicalResolver → TypeChecker → ByteCompiler → VM
```

TypeChecker runs after LexicalResolver so it can use resolved bindings. It produces a `TypeCheckResult` that ByteCompiler consumes:

```cpp
struct TypeCheckResult {
    TypeRegistry registry;
    std::unordered_map<const ast::Identifier*, std::string> known_types;  // node → type name
    std::unordered_set<const ast::BinaryExpression*> provably_true_is;    // skip PROT_CHECK
    std::unordered_set<const ast::CastExpression*> provably_safe_cast;    // skip PROT_CAST
    std::vector<DefaultMethodInjection> default_injections;               // for ByteCompiler
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};
```

### 10. What V1 Does NOT Include

- No generic types (List<Int>, Map<String, Circle>)
- No union types (int | string)
- No full type inference (only tracks what's explicitly annotated or narrowed)
- No interface inheritance (prot A : B)
- No readonly/mutability checking from type hints
- No cross-module type checking (only within a single compilation unit)

## Test Scenarios

1. `prot Drawable { fn draw() }` / `struct Circle : Drawable { fn draw() {} }` → passes
2. `prot Drawable { fn draw() }` / `struct Circle : Drawable { }` → error: missing draw
3. `prot Drawable { fn draw() { print("x") } }` / `struct Circle : Drawable { }` → default injection, passes
4. `fn foo(x: int) { foo("hi") }` → compile error: string not assignable to int
5. `if x is Drawable { x.draw() }` → no PROT_CHECK needed, direct dispatch
6. `x as UnknownProtocol` → compile error: protocol not defined
7. `struct Point : NonExistent` → compile error: protocol not defined
8. `let y: Circle = Circle(5)` → passes, y is known type Circle
9. `let y: Circle = 42` → compile error: int not assignable to Circle
10. `prot Drawable { fn draw(x: int) }` / `struct Circle : Drawable { fn draw(x: string) }` → warning: param type mismatch