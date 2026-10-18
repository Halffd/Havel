# Spec: remove-call-host

Scope: repo

# Remove CALL_HOST Opcode — Runtime Dispatch Spec

## Goal
Eliminate the `CALL_HOST` opcode and `host_global_names` infrastructure. All function calls use the uniform `LOAD_GLOBAL + CALL` pattern, with dispatch happening at runtime based on the callee value type (HostFuncId → host function, FunctionObjId/ClosureId → bytecode function, ObjectId → callable object).

## Architecture
- **Before**: Compiler knows about host functions at compile time via `host_global_names_`. Emits `CALL_HOST funcName, N` which bypasses the stack for the callee.
- **After**: Compiler emits `LOAD_GLOBAL funcName` (pushes HostFuncId onto stack) + `CALL N` (pops callee + N args, dispatches via `doCall()`). No compile-time knowledge of host functions needed.

## Conversion Pattern
```
// Before:
emit(OpCode::CALL_HOST, {Value::makeStringValId(strId), Value(N)});

// After:
emit(OpCode::LOAD_GLOBAL, Value::makeStringValId(strId));  // pushes callee
emit(OpCode::CALL, Value(N));                                // dispatches via doCall
```

**Important**: For Category A sites where args are already on the stack, `LOAD_GLOBAL` must be emitted BEFORE the args are pushed (so the stack order is: callee, arg0, ..., argN). For sites where the code pushes args then emits CALL_HOST, the LOAD_GLOBAL must move to before the arg-pushing code.

## Files Changed (8 + 2 test files)

### ByteCompiler.cpp (main work)
1. Delete orphaned/dead code at lines 3910-3970 (duplicate struct.new/class.new/host function paths after early returns)
2. Fix MemberExpression namespace call (lines 3982-4008): emit LOAD_GLOBAL objName + args + CALL_METHOD instead of CALL_HOST
3. Fix no-binding fallback (lines 4071-4122): remove `host_global_names_` reference, emit LOAD_GLOBAL + CALL
4. Fix HostFunction spread path (lines 4127-4172): emit LOAD_GLOBAL + CALL instead of CALL_HOST
5. Convert ~35 Category A CALL_HOST sites to LOAD_GLOBAL + CALL

### ByteCompiler.hpp
- Remove no-op `addHostGlobal()` (lines 39-41)
- Remove `host_global_names_ REMOVED` comment (line 208)

### BytecodeIR.hpp
- Remove `CALL_HOST` from OpCode enum (line 116)

### VM.cpp
- Remove CALL_HOST handler in main loop (lines 5003-5025)
- Remove CALL_HOST handler in sub-interpreter (lines 7997-8022)
- Remove dead doCall duplicate check (lines 3409-3414)
- Remove addHostGlobal loops (lines 8248-8250, 8525-8527)

### BytecodeOrcJIT.cpp
- Remove CALL_HOST JIT handler (lines 3514-3538)

### Pipeline.hpp + Pipeline.cpp
- Remove `host_global_names` field from PipelineOptions (line 25)
- Remove addHostGlobal loop (lines 610-612)
- Remove CALL_HOST disassembler case (lines 323-324)

### HostBridge.cpp
- Remove addHostGlobal loops (lines 433-434, 768-769)
- Remove CALL_HOST disassembler case (line 44)

### StdLibModules.cpp
- Remove all ~138 `host_global_names.insert()` lines (lines 98-295)

### BytecodeSmokeMain.cpp
- Remove 4 `host_global_names.insert()` lines (lines 349-352)
- Remove CALL_HOST disassembler case (lines 78-79)

### DebugUtils.cpp
- Remove CALL_HOST disassembler case (line 511)

### smoke_runner.cpp
- Remove CALL_HOST disassembler case (lines 80-81)

## Key Constraint: LOAD_GLOBAL Stack Order
For `CALL N`, the stack must contain: `[callee, arg0, arg1, ..., argN-1]`.
`CALL_HOST funcName, N` does NOT need callee on stack (it takes name as operand).
So when converting, `LOAD_GLOBAL funcName` must be emitted BEFORE args are pushed.

## Runtime Verification
- `LOAD_GLOBAL` already checks `globals` then `host_function_globals_` (VM.cpp:4605-4638)
- `doCall()` already dispatches `HostFuncId` to host function invocation (VM.cpp:3388-3401)
- `CALL` opcode already calls `doCall()` (VM.cpp:4982)
- All host functions are registered in `host_function_globals_` via `registerHostFunction()` (VM.cpp:714)