# Havel Execution Path Map — Phase 0 Ground Truth

Status: documentation only, no behavior change.
Scope: every path a `.hv` source can take from CLI to execution.
Derived from reading the code at main (`HavelLauncher.cpp`, `Pipeline.cpp`,
`ModuleLoader.cpp`, `Modules.cpp`); no measurements invented — measured values
are cited where they exist.

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

## 3. Self-hosted path (opt-in, slow — the known problem)

```
source.hv
    ↓ SelfHostedStrategy::execute
    ↓ modules/lang/launcher.hv (loaded and executed ON the VM)
        ↓ readScriptFile (launcher-side)
        ↓ tokenize(code)          ← Havel-implemented lexer, VM-interpreted
        ↓ parseTokens(tokens)     ← Havel Pratt parser,  VM-interpreted
        ↓ Havel AST
        ↓ inferProgram(result)    ← typecheck, also VM-interpreted
        ↓ compileProgram(emitter) ← emitter, VM-interpreted
        ↓ bytecode chunk (bc)
        ↓ vm.execute on that chunk
    ↓ VM
```

Measured (user script `hotkeys0.3.hv`, ~2200 lines):

```
TIMING: parse: tokenize = 1067 ms
TIMING: parse: parseAST = 225315 ms
TIMING: typecheck       = 8306 ms
TIMING: emit            = 56200 ms
total: 292.81 s user
```

Synthetic 5003-line fixture: >120 s (timeout) under `--self-hosted`.

Root cause: the compiler compiles the compiler with the interpreter before it
can compile the user program (bootstrapping architecture problem). NOT Pratt
complexity per se.

Mitigation already in tree: ScriptStrategy (default) never enters this path;
`--self-hosted` is explicit. This path remains for bootstrap verification /
REPL / development.

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
--target jit      → cfg.useJIT = true; VM tiering backend (ORC LLJIT)
--emit-llvm/-asm  → runBuild + BytecodeOrcJIT::translate per function → .ll / .s
--target aot + --build → .hvc + .o (LLVM machine code) + .so (shared object)
--full-aot        → everything above + stub.cpp linked into native executable
                    (incremental cache keyed, e.g. "AOT ELF stored in
                     incremental cache (key c11d85872406)")
--target aot (no --build) → parsed into cfg.target but NOT dispatched to the
                    AOT emission path; falls through to ScriptStrategy
                    (known gap — ticket item #45 "separate compile from execute")
```

Cranelift backend exists as `src/havel-lang/compiler/cranelift-backend/`
(Rust) but is not linked through CMake; no working integration.

---

## 6. Known gaps mapped to ticket items

| Gap | Ticket item |
|-----|-------------|
| `--target aot` without `--build` does nothing | #8 separate compile/execute, #45 |
| engine.execute(source) couples parse+execute | #8 |
| incremental/ library not wired into main pipeline | #10, #35 |
| self-hosted parse ~0.2-0.3 s/line (structural) | #6, #14, #37, #38 |
| no large-script benchmark suite | #17 |
| no compiler timing diagnostics (self-hosted has ad-hoc `measure()`) | #42 |
| host builtins shadow .hv modules of same name | #18, #24 |
| .hvc cache identity: source hash + fingerprint present for new caches; legacy caches hash-less | #11 (mostly done; legacy migration open) |

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
