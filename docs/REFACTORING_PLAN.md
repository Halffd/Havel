# Havel Architecture Refactoring Plan

## Current State (Good)

- 3.7k lines in Interpreter.cpp (reasonable for tree-walker)
- Clean AST visitor pattern
- Proper environment scoping
- Module system exists

## Current State (Needs Work)

- Host APIs coupled to interpreter
- No clear module boundaries  
- Stdlib functions mixed with core semantics
- Hard to test language core in isolation

## Phase 1: Extract Host Context (Week 1-2) ✅ DONE

**Created:** `src/havel-lang/runtime/HostAPI.hpp`

Abstract interface for host operations. Interpreter will depend on this instead of concrete managers.

## Phase 2: Simple Module System (Week 2-3) ✅ DONE

**Created:** `ModuleLoader` - simple, explicit, no singletons

```cpp
ModuleLoader loader;
loader.add("array", registerArrayModule);
loader.addHost("window", registerWindowModule);
loader.loadAll(env, hostAPI);
```

**Key principles:**
- No singletons (each Interpreter has its own loader)
- No static registration (explicit function calls)
- No macros (simple registration)
- Clear stdlib vs host separation

## Phase 3: Split Stdlib (Week 3-4) - IN PROGRESS

Most stdlib modules already exist as separate files. Just need to:
1. Ensure consistent registration pattern
2. Register via ModuleLoader
3. Remove any remaining inline builtins from Interpreter

## Phase 4: Module Loader Cleanup (Week 4-5)

Split ModuleLoader.cpp into:
- `StdLibModules.cpp` - registers only stdlib
- `HostModules.cpp` - registers only host modules

## Immediate Actions (This Week)

1. ✅ Create IHostAPI interface
2. ✅ Create simple ModuleLoader
3. ⏳ Update existing modules to use ModuleLoader
4. ✅ Document module patterns

## Success Metrics

- [ ] ModuleLoader used consistently
- [ ] Zero direct dependencies on WindowManager/HotkeyManager in Interpreter
- [ ] Language core testable without GUI
- [ ] Clear module API documentation
- [ ] Build time < 60 seconds

## Files That Should NOT Be Touched Yet

- Async scheduler (working well)
- Channel implementation (solid)
- Trait system (good design)
- Config system (works)

## Files That MUST Be Refactored

- `Interpreter.cpp` - Extract host API usage
- `ModuleLoader.cpp` - Split core vs host modules
- `Environment.hpp` - Clean up module registration

---

## Key Insight

**Your architecture is 80% correct already.** 

The visitor pattern is clean. The environment system works. The module system exists.

The problem is just **coupling**, not fundamental design.

Small, incremental refactors will fix this without breaking everything.

## Simple Module Pattern

```cpp
// 1. Declare in header
void registerArrayModule(Environment& env);

// 2. Implement in cpp
void registerArrayModule(Environment& env) {
    env.Define("push", BuiltinFunction(...));
}

// 3. Register in loader
loader.add("array", registerArrayModule);

// 4. Load
loader.load(env, "array");
```

That's it. No macros. No singletons. No magic.
