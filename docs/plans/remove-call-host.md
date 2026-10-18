---
plan name: remove-call-host
plan description: Remove CALL_HOST opcode
plan status: done
---

## Idea
Remove the CALL_HOST opcode and host_global_names infrastructure entirely. Replace all CALL_HOST emissions in ByteCompiler.cpp with LOAD_GLOBAL + CALL, relying on runtime dispatch via doCall(). This eliminates the compile-time host function list, removes duplicated dispatch paths, and enables dynamic registration of new functions at runtime. The VM already supports this: LOAD_GLOBAL looks up host_function_globals_ (populated by registerHostFunction), and doCall dispatches HostFuncId values to host function invocations. The change affects 8 files: ByteCompiler.cpp (48 CALL_HOST sites + dead code + broken host_global_names_ ref), ByteCompiler.hpp (remove no-op addHostGlobal), BytecodeIR.hpp (remove CALL_HOST enum), VM.cpp (remove 2 CALL_HOST handlers + dead doCall duplicate), Pipeline.hpp/cpp (remove host_global_names field + loop), HostBridge.cpp (remove addHostGlobal loops + disassembler case), StdLibModules.cpp (remove 138 host_global_names.insert lines), BytecodeSmokeMain.cpp (remove 4 host_global_names.insert lines + disassembler case), DebugUtils.cpp (remove disassembler case), smoke_runner.cpp (remove disassembler case), BytecodeOrcJIT.cpp (remove CALL_HOST JIT handler).

## Implementation
- Step 1: ByteCompiler.cpp compileCallExpression — Delete dead/orphaned code (lines 3910-3970) that remains after early returns from struct.new/class.new/HostFunction paths. This is unreachable code using old CALL_HOST pattern.
- Step 2: ByteCompiler.cpp compileCallExpression — Fix MemberExpression namespace call (lines 3982-4008): Replace CALL_HOST emission with LOAD_GLOBAL of namespace object + CALL_METHOD. The current code says 'Always emit LOAD_GLOBAL + CALL_METHOD' in comment but emits CALL_HOST instead.
- Step 3: ByteCompiler.cpp compileCallExpression — Fix no-binding fallback (lines 4071-4122): Remove host_global_names_ reference (compile-breaking bug at line 4077). Replace entire block with LOAD_GLOBAL + CALL. If the global doesn't exist, LOAD_GLOBAL will throw at runtime.
- Step 4: ByteCompiler.cpp compileCallExpression — Fix HostFunction binding with spread (lines 4127-4172): Replace CALL_HOST emission with LOAD_GLOBAL + CALL. This is the second HostFunction check path that handles spread args.
- Step 5: ByteCompiler.cpp Category A sites — Convert all ~35 simple CALL_HOST emissions to LOAD_GLOBAL + CALL. Pattern: emit(LOAD_GLOBAL, strId) before args, emit(CALL, N) after args. Sites: sleep(1360), config.set(1441), mode(1464), mode.register(1563), struct.define(1622), class.define(1666), class.method(1708,1756), any.extend(2360), any.in(2686), regex_search(2695), config.append(2728), any.* pipe(2825), async.await(3479), any.len(3690), runCapture/backtick(3774,3786), type/assert(5303,5333), Hotkey(5531,5659), hotkey.register(5581,5700), io.*(5718-5772), run(5796,5807), sleep(5832), io.get(5864), thread.spawn(5883), interval.start(5906), timeout.start(5929), thread_spawn(6113), channel.new(6170).
- Step 6: BytecodeIR.hpp — Remove CALL_HOST from OpCode enum (line 116).
- Step 7: VM.cpp — Remove CALL_HOST handler in main interpreter (lines 5003-5025), remove CALL_HOST handler in sub-interpreter (lines 7997-8022), remove dead doCall duplicate (lines 3409-3414).
- Step 8: BytecodeOrcJIT.cpp — Remove CALL_HOST JIT handler (lines 3514-3538). The CALL opcode already handles HostFuncId dispatch via doCall in the JIT path.
- Step 9: Pipeline.hpp — Remove host_global_names field from PipelineOptions (line 25). Pipeline.cpp — Remove addHostGlobal loop (lines 610-612) and CALL_HOST disassembler case (lines 323-324).
- Step 10: ByteCompiler.hpp — Remove no-op addHostGlobal() method (lines 39-41) and the host_global_names_ REMOVED comment (line 208).
- Step 11: HostBridge.cpp — Remove addHostGlobal loops (lines 433-434, 768-769) and CALL_HOST disassembler case (line 44).
- Step 12: StdLibModules.cpp — Remove all ~138 host_global_names.insert() lines (lines 98-295).
- Step 13: BytecodeSmokeMain.cpp — Remove 4 host_global_names.insert() lines (lines 349-352) and CALL_HOST disassembler case (lines 78-79).
- Step 14: DebugUtils.cpp — Remove CALL_HOST disassembler case (line 511). smoke_runner.cpp — Remove CALL_HOST case (lines 80-81).
- Step 15: Build and test — Run ./build.sh 6 build then ./build.sh test to verify everything compiles and passes.
- Step 16: VM.cpp addHostGlobal calls — Remove addHostGlobal loops at lines 8248-8250 and 8525-8527 (both iterate host_function_globals_ calling the no-op addHostGlobal).

## Required Specs
<!-- SPECS_START -->
- remove-call-host
<!-- SPECS_END -->