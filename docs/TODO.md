# Havel TODO

## Recent Fixes (Completed ✅)

### Parser & Compiler (March 2026)
- [x] Fixed Parser returning nullptr for `config.method()` calls (keyword fallthrough bug)
- [x] Fixed string member calls stack underflow (missing object push)
- [x] Fixed config.load() use-after-free in destructor
- [x] Fixed Config template bug (ostringstream adding quotes)
- [x] Added error recovery to parser (synchronize on errors)
- [x] Added forward progress guarantee in parser (prevent infinite loops)
- [x] Fixed HostBridge memory leak (shared_from_this circular reference)
- [x] Fixed WindowMonitor duplicate instance creation
- [x] Fixed AutomationSuite null IO warnings
- [x] Fixed GUI flag in runScript() mode
- [x] Added help() function with module documentation
- [x] Added detailed debug output for initialization

---

## Type System Implementation (In Progress)

### Foundation (Completed ✅)
- [x] HavelType base class with Kind enum
- [x] HavelStructType with field definitions
- [x] HavelEnumType with variant definitions
- [x] TypeRegistry singleton for type storage
- [x] TypeChecker with TypeMode (None, Warn, Strict)
- [x] HavelValue with optional annotatedType field
- [x] Parser support for struct/enum definitions
- [x] Parser support for type annotations
- [x] CLI `--run` mode for pure script execution

### Semantic Analysis (TODO)
- [ ] Create `SemanticAnalyzer` class for type checking pre-pass
  - [ ] `registerStructTypes()` - Register all struct definitions in TypeRegistry
  - [ ] `registerEnumTypes()` - Register all enum definitions in TypeRegistry
  - [ ] `validateTypeAnnotations()` - Validate type references exist
  - [ ] `inferTypes()` - Type inference for untyped variables
  - [ ] `checkFunctionSignatures()` - Validate function parameter/return types
  - [ ] `buildSymbolTable()` - Build symbol table with type information

### Syntax Analysis Enhancements (TODO)
- [ ] Create `SyntaxValidator` class for syntax-level checks
  - [ ] `validateStructSyntax()` - Validate struct field syntax
  - [ ] `validateEnumSyntax()` - Validate enum variant syntax
  - [ ] `validateTypeAnnotationSyntax()` - Validate type annotation syntax
  - [ ] `validateStructConstruction()` - Validate struct literal syntax
  - [ ] `validateEnumConstruction()` - Validate enum variant construction

### Interpreter Integration (IN PROGRESS)
- [x] Implement TypeChecker::checkCompatibility() method (declared, implementation pending)
- [x] Implement TypeChecker::validateStructFields() method (declared, implementation pending)
- [ ] Update visitMemberExpression() for struct field access
- [ ] Implement struct construction with type validation
- [ ] Implement enum construction with variant validation
- [ ] Add type coercion rules
- [ ] Add error messages for type mismatches
- [ ] Fix all Interpreter.cpp locations to use HavelValue::data member
- [ ] Fix Engine.cpp for ReturnValue::value shared_ptr
- [ ] Update ValueToString and other variant operations

### Testing (TODO)
- [ ] Create test scripts for type system
  - [ ] test_types.hv - Basic type tests
  - [ ] test_structs.hv - Struct definition and usage
  - [ ] test_enums.hv - Enum definition and pattern matching
  - [ ] test_type_errors.hv - Type error detection
- [ ] Run tests with `havel --run scripts/test_*.hv`

---

## Missing Features

### Core Features
- [x] Window Show/Hide methods implementation
- [ ] Complete Wayland support for Window/WindowManager (currently placeholders)
- [ ] Implement multi-hotkey declaration support (e.g., `H => F5 => audio.mute()`)
- [ ] Full mode system support for ConditionSystem
- [x] Help builtin supporting all modules/functions documentation/args
- [ ] Improve screenshot manager, with io hotkeys, screenshot view window and current monitor screenshot
commit

### Havel Language
- [ ] LLVM JIT compilation support
- [ ] More comprehensive error messages with line numbers
- [ ] Debugger integration
- [ ] Language server protocol (LSP) support
- [ ] More standard library functions

### Build System
- [ ] Fix linker warnings for readline library conflicts
- [ ] Optimize build times
- [ ] Better dependency management

### Documentation
- [ ] Complete API documentation
- [ ] More code examples
- [ ] Tutorial series
- [ ] Video demonstrations

### Testing
- [ ] Increase test coverage
- [ ] Integration tests
- [ ] Performance benchmarks
- [ ] Stress testing

### GUI
- [ ] More themes and customization
- [ ] Better error reporting in GUI
- [ ] Configuration editor
- [ ] Profile management UI

### Platform Support
- [ ] Better multi-monitor support
- [ ] Touchscreen support
- [ ] Gamepad/controller support
- [ ] More window manager compatibility

## Known Issues
- Segfaults in certain edge cases (see IMPLEMENTATION_STATUS.md)
- Some X11 event handling race conditions
- Memory leaks in long-running sessions (needs profiling)

## Future Enhancements
- Plugin system
- Remote control via network
- Cloud sync for configurations
- AI-powered automation suggestions
- Voice control integration
