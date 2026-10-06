# Havel Execution Path Map — Phase 0 Ground Truth

Status: documentation only, no behavior change.
Scope: every path a `.hv` source can take from CLI to execution.
Derived from reading the code (first pass) and re-verified against the tree on
2026-10-05; no measurements invented — every measured value below is either
cited from the ticket report that motivated the work or reproduced here on
`build-headless`.

---

## 1. CLI entry graph

```
havel [flags] files...
    ↓
HavelLauncher::parseArgs()          → LaunchConfig {mode, target, ...}
    ↓
HavelLauncher::execute(cfg)
    ├─ cfg.buildOnly                 → runBuild(cfg)
    ├─ cfg.diffPipelinePath nonempty → diffPipeline(cfg)
    └─ createStrategy(cfg.mode)
         ├─ Mode::DAEMON            → DaemonStrategy
         ├─ Mode::SCRIPT            → ScriptStrategy
         ├─ Mode::REPL/SCRIPT_AND_REPL → ...
         ├─ Mode::TEST              → TestStrategy
         ├─ Mode::CLI               → CliStrategy
         ├─ Mode::SELF_HOSTED       → SelfHostedStrategy   (opt-in: --self-hosted)
         └─ default                 → CliStrategy
```

Mode promotion rules (current state, after commit `8e06411d`):

- `--self-hosted` sets `Mode::SELF_HOSTED` unconditionally (explicit opt-in).
- No filesystem-based auto-promotion into SELF_HOSTED remains.
- Default for `havel script.hv` is the native C++ pipeline (ScriptStrategy).

---

## 2. Native script path (default, fast)

```
source.hv
    ↓ readScriptFile / loadScriptFiles (concatenate multi-file inputs)
    ↓ parser::Parser::produceAST(code)          [src/havel-lang/parser/Parser.cpp]
    ↓ AST
    ↓ ByteCompiler (compileToBytecodeChunk)     [src/havel-lang/compiler/core/ByteCompiler.cpp]
    ↓ BytecodeChunk
    ↓ runBytecodePipeline / engine.execute      [Pipeline.cpp, HavelEngine]
    ↓ VM execution                              [VM.cpp, VMDispatch]
```

Measured: sub-second for multi-thousand-line scripts
(5003-line synthetic fixture: ~0.34 s total wall).

Caching on this path:

- `runBuild` (`--build FILE`) writes `.hvc` to `~/.cache/havel/` with the flat
  cache name derived from the canonical source path
  (`<stem>.<path-hash>.hvc` for script files, `lang.<stem>`/`std.<stem>` for
  module trees).
- Cache reuse validation (single-file and build-many): embedded source hash
  (sha256) + size + pipeline fingerprint + compile flags — plain mtime is
  NOT trusted (GLBS-trailer rewrites bump mtime without recompiling).

---

## 3. Self-hosted path (opt-in)

### 3a. How user source actually gets compiled (current, fast)

```
source.hv
    ↓ SelfHostedStrategy::execute
    ↓ parseScript(combinedCode, cfg)          [native, only to spot hotkeys/UI]
    ↓ compiler::NativeCompiler::compileSource  [the CompilationService boundary]
    ↓ runBytecodePipeline → CompiledUnit{.chunk, .sourcePath, .fromCache}
    ↓ serializeChunk → ~/.cache/havel/<stem>.<path-hash>.hvc
    ↓ appArgList receives .hvc paths (+ --script-dir for module resolution)
    ↓ modules/lang/launcher.hv (loaded and executed ON the VM)
        ↓ runScript: input ends in .hvc → runBytecodeFiles
        ↓ VM executes the bytecode directly
```

The launcher still runs on the VM and still owns REPL / `--eval` / module
resolution. It just never sees the user's source: it receives bytecode. The
`.hvc` is keyed by the canonical source path and validated against source hash,
pipeline fingerprint and compile flags, so a stale artifact cannot serve.

Measured 2026-10-05 (`build-headless`, headless, 9333-line fixture, cold cache),
user-source compile stage times from the `HAVEL_STARTUP_TIMING` channel:

```
[compile] source:     .../parse_scaling_fixture.hv
[compile] frontend:   native
[compile] cache:      miss
[startup] compile.tokenize+parse = 20.13ms
[startup] compile.typecheck       =  3.48ms
[startup] compile.semantic        =  8.37ms
[startup] compile.emit            = 26.77ms
[startup] compile.total           = 58.84ms
TIMING: precompiled: load = 3ms
```

End-to-end wall: native 4820 ms, self-hosted 4290 ms. Both dominated by ~3 s of
fixed startup (module/plugin loading), not by compilation.

Scaling of the user-source compile (same build, self-hosted precompile, each
size a fresh path so the cache cannot hit):

| fixture lines | tokenize+parse | typecheck | semantic | emit | total |
|---------------|-----------------|----------|----------|------|-------|
| 3203          | 14.46 ms        | 3.50 ms  | 5.93 ms  | 20.52 ms | 44.62 ms |
| 12203         | 37.16 ms        | 3.58 ms  | 23.58 ms | 81.96 ms | 146.43 ms |
| 24203         | 135.20 ms       | 15.50 ms | 55.50 ms | 326.79 ms | 533.05 ms |
| 48202         | 270.69 ms       | 17.69 ms | 179.71 ms | 416.46 ms | 887.14 ms |

Roughly linear; emit and semantic analysis are the dominant stages and are the
only ones with visible superlinear slope. Type checking is flat.

Guard: `scripts/check_selfhost_fastpath.sh <build-dir>` asserts on a >5000-line
fixture that both paths print identical output, that `--self-hosted` reported
`precompiled: load`, that no interpreted `TIMING: parse:` stage ran, and that
the wall clock stayed inside a bound.

### 3b. The interpreted compiler path (still reachable, no longer on the script path)

When source does reach the launcher as text — `--eval`, REPL input, or any
caller that hands over source instead of a `.hvc` path — the launcher runs it
through the Havel-implemented compiler on the VM:

```
source
    ↓ tokenize(code)          ← Havel lexer, VM-interpreted
    ↓ parseTokens(tokens)     ← Havel Pratt parser, VM-interpreted
    ↓ inferProgram(result)    ← typecheck, VM-interpreted
    ↓ compileProgram(emitter) ← emitter, VM-interpreted
    ↓ bytecode chunk → vm.execute
```

Measured here before Milestone A landed (ticket report, `hotkeys0.3.hv`,
~2200 lines): tokenize 1067 ms, parseAST 225315 ms, typecheck 8306 ms, emit
56200 ms — 292.81 s total. A 5003-line fixture did not finish in 120 s.

That cost is structural, not Pratt complexity: the compiler compiles the
compiler with the interpreter before it can compile the program. It is the
reason 3a exists, and it is why `--lint`/`--eval`/REPL remain slow on large
inputs. Replacing it (JIT-ing the parser, or bootstrapping the compiler itself
from a prebuilt `.hvc`) is follow-up work, not part of the script path.

---

## 4. Native module path

```
Havel++ module (C++ sources under src/modules/, src/havel-lang/stdlib/*Module.cpp)
    ↓ compiled at build time into havel_mod_<name>.so plugins
      (or static archive havel_modules.a when ENABLE_MODULE_PLUGINS=OFF)
    ↓ ModuleLoader::resolve(name)
        1. flat bytecode cache check (lang.<name>.hvc / std.<name>.hvc)
        2. script-dir + searchPaths source (.hv)
        3. havel_mod_<name>.so plugin
        4. HostBuiltin (registered host functions, e.g. "window.*" via UIBridge)
    ↓ host functions exposed as module namespace in VM
    ↓ scripts call window.pos(...) etc.
```

Note (observed this session): for module names registered as host builtins
(`window`, `math_native`, `sys`, ...), the C++ host module shadows any same-named
`.hv` source; `use window` never opens `modules/app/window.hv`.

---

## 5. AOT / JIT paths

```
--target jit      → cfg.useJIT = true; VM tiering backend (ORC LLJIT) when
                    ENABLE_LLVM=ON. Without LLVM the flag is accepted but
                    changes nothing: no backend is linked, everything runs
                    interpreted (measured: identical output and profiler
                    tier1=0 tier2=0 under --target jit and --target
                    interpret in an ENABLE_LLVM=OFF build).
--emit-llvm/-asm  → runBuild + BytecodeOrcJIT::translate per function → .ll / .s
--target aot + --build → .hvc + .o (LLVM machine code) + .so (shared object)
--full-aot        → everything above + stub.cpp linked into native executable
                    (incremental cache keyed, e.g. "AOT ELF stored in
                     incremental cache (key c11d85872406)")
--target aot (no --build) → --target aot itself sets buildOnly + emitObj +
                    emitBinary, so it IS the compile-only stage (this is
                    the "separate compile from execute" the ticket asks
                    for; `havel run file.hv` is the execute stage). In an
                    ENABLE_LLVM=OFF build it compiles the .hvc, then fails
                    cleanly: "AOT compilation requires LLVM support.
                    Rebuild with ENABLE_LLVM=ON", exit 1 (measured).
```

Cranelift backend: `src/havel-lang/compiler/cranelift-backend/` is a Rust
staticlib (single lib.rs, ~3.5k lines) that CMake builds via cargo when
`ENABLE_CRANELIFT=ON` (build.sh modes 17/18/19: build-crane,
build-crane-nollvm, build-crane-release) and links into `havel_runtime`.
The VM constructs it as the tier-1 "fast" backend of the TieredBackend
composite (tier 2 = ORC); `can_lower()` declines any function outside its
documented opcode subset, so a declined function stays interpreted rather
than being partially compiled. Status in this environment: OFF in
build-headless, never exercised here; the intended verification is the
cranelift_proto_driver test (ENABLE_TESTS + ENABLE_CRANELIFT), which was
not built. ORC JIT: BytecodeOrcJIT.cpp is excluded from the build unless
ENABLE_LLVM=ON (CMakeLists.txt ~767). LLVM 23.1.1 dev packages are
installed on this machine, so an LLVM build is possible; none was produced
in this session, so every measurement in this document comes from the
interpreter.

---

## 6. Known gaps mapped to ticket items

| Gap | Ticket item | Status |
|-----|-------------|--------|
| `--target aot` without `--build` does nothing | #8 separate compile/execute, #45 | wrong as stated: `--target aot` sets buildOnly+emitObj+emitBinary, i.e. it IS compile-only; dispatches to the build path and fails cleanly (exit 1, explicit error) in LLVM-off builds |
| engine.execute(source) couples parse+execute | #8 | partly addressed: `CompilationService`/`CompiledUnit` split the boundary, but `HavelEngine::execute` still takes source and drives the VM |
| incremental/ library not wired into main pipeline | #10, #35 | partly addressed: the script/precompile path validates `.hvc` through `loadCachedScriptChunk`; `IncrementalDriver` (fingerprints, DependencyGraph, TieredCache) is only reached by the AOT ELF step |
| self-hosted parse ~0.2-0.3 s/line (structural) | #6, #14, #37, #38 | mitigated on the script path (§3a). Still true for `--eval`/REPL/large `--lint`, which hand source to the launcher (§3b) |
| no large-script benchmark suite | #17 | partial: `scripts/check_selfhost_fastpath.sh` covers scaling + parity at >5000 lines; no general benchmark harness |
| no compiler timing diagnostics (self-hosted has ad-hoc `measure()`) | #42 | done: `[compile] source/frontend/cache` + `HAVEL_STARTUP_TIMING` per-stage timings (`compile.tokenize+parse`, `.typecheck`, `.semantic`, `.emit`, `.total`) |
| host builtins shadow .hv modules of same name | #18, #24 | open |
| .hvc cache identity: source hash + fingerprint present for new caches; legacy caches hash-less | #11 | mostly done; legacy migration open |

---

## 7. Dependency direction (current, observed)

```
havel scripts (.hv)
    ↓ use <module>
ModuleLoader (cache → source → plugin → HostBuiltin)
    ↓
host bridges (ModularHostBridges.cpp) — arg parsing, Value shapes
    ↓
services (WindowService, PixelAutomationService, ...)
    ↓
WindowManager / backends (X11Backend, Wayland, ...)
    ↓
X11 / Wayland / D-Bus
```

Compiler internals (AST, tokens, parser) are NOT exposed to native modules
today — modules depend on Runtime ABI (VMApi, Value, host functions). This
matches ticket item #19 already; it must be preserved.
