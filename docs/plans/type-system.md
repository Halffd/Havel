---
plan name: type-system
plan description: Compile-time type checking and protocol verification
plan status: active
---

## Idea
Redesign Havel's protocol and type hint system to be Swift/TypeScript-like:

**Current state**: 
- Type hints exist in the AST but are only used for runtime assertions (emitTypeAssertionForLocal calls type() + assert())
- Protocol declarations are parsed but their ByteCompiler handling is a no-op (just break)
- Protocol conformance is registered at Pipeline level but never validated - no compile-time check that a struct/class implementing a protocol actually provides all required methods
- Custom nominal types (class/struct names, protocol names) return nullopt from normalizeTypeAnnotation with comment "not enforceable yet"
- No TypeChecker exists in the semantic layer

**Target state** (Swift/TypeScript-like):
1. **TypeChecker** - New semantic analysis pass after LexicalResolver, before ByteCompiler
2. **Protocol contract verification** - Compile-time check: if `struct Circle : Drawable`, verify Circle has all Drawable's required methods (signatures match)
3. **Type hint enforcement** - `fn draw(x: int): string` validates at compile time where possible, falls back to runtime assertions for dynamic values
4. **Protocol-aware type narrowing** - `if x is Drawable { x.draw() }` — type checker knows x is Drawable inside the branch
5. **Protocol default methods** - If protocol provides a defaultBody, auto-inject into conforming types
6. **Type optimization** - ByteCompiler uses type checker results to emit optimized bytecode (skip PROT_CHECK when statically known, specialized dispatch)

The key insight: Swift checks protocol conformance at compile time. TypeScript checks structural types at compile time. We should do both - structural verification that a type has the right methods, plus nominal tracking of which types declared conformance to which protocols.

## Implementation
- Create TypeChecker class in src/havel-lang/compiler/semantic/TypeChecker.hpp with TypeEnv, TypeVar, ProtocolInfo data structures
- Implement TypeChecker::check(Program) - first pass: collect all type/protocol/struct/class declarations into a TypeRegistry
- Implement protocol contract verification: when struct X : Proto, verify X has all Proto required methods with matching signatures
- Implement type hint resolution: resolve TypeAnnotation nodes to concrete types from TypeRegistry, emit compile errors for unknown types
- Implement type narrowing for is/as: track type guards in if/when branches so subsequent method calls are verified
- Implement protocol default method injection: if conforming type is missing a method but protocol has defaultBody, mark for auto-injection
- Wire TypeChecker into Pipeline.cpp between LexicalResolver and ByteCompiler, report errors via ErrorReporter
- Update ByteCompiler to use TypeChecker results: skip PROT_CHECK when statically known, use type hints for optimized dispatch
- Write comprehensive test scripts: protocol contract errors, type hint errors, type narrowing, default methods, type-optimized dispatch
- Build, test, fix regressions, commit and push

## Required Specs
<!-- SPECS_START -->
- remove-call-host
- type-system-design
<!-- SPECS_END -->