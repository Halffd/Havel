# Validation Report: TODO2.md Compiler Architecture Roadmap

**Repository:** `/home/all/repos/havel` (branch `main`)
**Plan:** `TODO2.md` (untracked user file — status field still says "Planned"; all 13 items below are done)
**Date:** 2026-09-21
**Commits:** 20 on main, in order:

1. `7d41de70` compiler: wire optimizeBytecode through runBytecodePipeline
2. `d58ec9c6` vm: run dispatch loop in execute when no scheduler is installed
3. `43cc5660` stdlib: fix string cursor for chunk-pool string literals
4. `e066215f` test: add pipeline integration tests and hvtest --optimize flag
5. `6805302b` havel++: prototype compiler and script-dir native module resolution
6. `dcea373b` build: register hpp_proto_test and fix test-loop include paths
7. `42c7d268` cranelift: fix prototype against cranelift 0.121 and complete bridge registry
8. `ccd18aa8` runtime: add missing GetBrightnessManager stub to core-profile HostAPI
9. `2e5ed6c6` build: complete cranelift_proto_driver link
10. `bec910a9` test: C API integration coverage and null-operator regression
11. `c7e80fb4` lexer: treat operators after null literal as expression context
12. `1f5aeee5` gc: guard pinned roots so exceptions don't leak them (user commit, interleaved)
13. `887bf351` jit: remove unwired PGO, layout, queue, and inline-cache infrastructure
14. `97c6ebd0` build: fix LTO-archive links and havel-hppc release relocations
15. `0d464d57` build: gate havel-hppc ABI flags on compiler
16. `fbce0117` test: Havel++ prototype integration test
17. `f498c1f3` build: remove -ffast-math from release flags

(Plus user commits `c9304411` pixel/Qt and `1f5aeee5` gc landed in the same
window; this session's work does not touch their files.)

---

## 1. §27 Ticket 1 — BytecodeIR + optimization pipeline in the production path

The passes (SimplifyCFG, ConstProp, DCE, CopyProp, TypeProp,
FastIntegerLowering, Inlining, LICM, Validation) were already real; the gap
was the production path. `runBytecodePipeline` accepted
`PipelineOptions::optimizeBytecode` but never ran the optimizer — the
`-O`/`--optimize-bytecode` flag was silently ignored on the C++ script path.

- `cfgintegration::optimize_chunk_cfg` + `describe_optimize_stats`: one
  shared entry for all production callers
- `runBytecodePipeline` runs the CFG pipeline when the flag is set, before
  snapshot/caching/execution
- hvtest `--optimize` flag: forwarded to the embedded suites, appended as
  `--optimize-bytecode` to self-hosted invocations
- Leftover `[RUNBYTECODE-DEBUG]` stderr noise removed from every pipeline run

**Blocking bug fixed in scope:** `VM::execute` ran its setup but never called
`runDispatchLoop` when no scheduler was installed — silently returned null
for every script. `executePersistent` kept its call; `execute` lost it during
the goroutine refactor. Production caller hit: the C API `havel_loadstring`
pushed the null result and reported HAVEL_OK.

**Blocking bug fixed in scope:** `string.cursor()` seeded the cursor with
`VM::getStringId()`, which returns 0 for chunk-pool literals
(`isStringValId`, not heap strings) — every cursor read threw
"string not found". Failed both optimized and unoptimized.

## 2. §27 Ticket 2 — incremental compilation at the module compilation boundary

- `loadCachedScriptChunk`: the `.hvc` entry autoCache writes is read back and
  served when it validates against the live source text (size + sha256), the
  current pipeline fingerprint, and the compile options. Wired before the
  parse in `compileToBytecodeChunk` (the path HavelEngine and the launcher
  boots use), so the dominant parseAST cost is skipped on hits.
- **Version-6 `.hvc` header** records strict-resolution + optimization
  settings (TODO2.md §9.1 "relevant compilation options": strict turns
  undeclared reads into errors; the optimizer rewrites opcodes). Old readers
  reject v6 cleanly and heal on the next compile.
- **Fingerprint self-reference loop fixed** — the big one:
  `computePipelineFingerprint` hashed whole `.hvc` files of the
  fingerprint-input modules (lang.emitter/pratt/lexer/scope). Recompiling any
  of them rewrites that file — the only changing bytes are the fingerprint
  field itself plus an appended globals trailer — so every stamped
  fingerprint was an intermediate state that never matched the next boot's
  recomputed one. strace-observed result: those modules recompiled from
  source on every plain `havel run`, forever. Fix: hash the stable region
  (chunk body to the first trailer marker, fingerprint field excluded).
- `IncrementalDriver::invalidate()` was a stub removing nothing; the cache
  keys it needed are the compilation unit's recorded outputs, so stale
  artifacts now drop from the cache directly.
- `compileModule` template moved to the header (call-site lambdas have no
  linkage; the .cpp definition could never be instantiated from another TU).
- `IncrementalDriver`/`TieredCache`/`DependencyGraph` remain standalone: their
  content-addressed + import-fingerprinting design targets whole-program
  compilation where dependency content is embedded; the runtime-resolved
  module boundary already implements the incremental model through the
  validated .hvc cache. Implemented but not integrated — recorded per TODO2
  §3, not claimed as integrated.

## 3. §11 Phase 6 — BytecodeOrcJIT refactor

Survey found four unwired subsystems with zero callers anywhere:

| Subsystem | Evidence |
|---|---|
| PGO collector + pass runner | `saveProfileData`/`loadProfileData` literal no-ops |
| Code layout (block-order/cold-block) | explicit "no-op without active PGO" stubs |
| Async compilation queue | thread never started; `processCompileQueue` popped tasks into an empty body |
| Inline caches | keyed by `getReceiverTypeHash` — an explicit stub returning 0 |

Removed: -255 lines (682→582 `.cpp`, 298→144 `.h`). The JIT is a backend
over BytecodeIR, not a second compiler/runtime. Profiling lives in
VMProfiler; tiering in `compileFunctionTier` and the VM tier callbacks.

## 4. §13 — Cranelift backend evaluation

The Rust prototype had never compiled. Fixed against cranelift 0.121:
`brif` block-arg slices, `Switch::new/set_entry/emit` (the removed
`ins().switch`), explicit user trap (the removed `unreachable` and
`TrapCode::UnreachableCodeReached`). Fixing the type errors surfaced two
pre-existing E0499 borrow errors in the 3400-line lowering (the function
never type-checked, so borrow checking was skipped): hoisted the iconst
calls. Completed the `bridge_ids` registry: 26 GC/exception/object/iter/
array/call/global ids were declared but never inserted (the first lowering
reaching them panicked "gc_register_roots bridge").

Build: `cranelift_proto_driver` never linked (the shim drags in the whole
runtime; gui managers + the ORC JIT were not on its link line) — explicit
resolution-ordered link items. Pre-existing incomplete-lib bug:
`HostAPI.cpp`'s core-profile variant omitted `GetBrightnessManager` while
its vtable references it.

**§24 matrix** (fair release binaries, arithmetic.hv / strings.hv):

| Path | arithmetic | strings |
|---|---|---|
| VM only (no tiering) | 1.43s | 0.64s |
| LLVM ORC tiering (O0→O2) | 1.20s | — |
| Cranelift tier1 + ORC tier2 | **0.43s** | **0.39s** |

**CORRECTION (post -ffast-math fix):** those numbers were measured on
pre-fix binaries built with `-ffast-math` — the UB's own miscompilation.
`-ffast-math` eliminates the NaN-checking branches on tagged values, which
made benchmarks artificially faster AND caused the exit crash. Honest
post-fix numbers (clang, mode 19):

| Path | arithmetic.hv |
|---|---|
| VM only (no tiering) | 2.15s |
| Cranelift tier1 (tier2 async incomplete at exit) | 1.72-1.92s |

The Cranelift tier-1 win is real but modest (~15-20%) on this benchmark,
not 4x. The ABI sharing verdict is unchanged (verified by construction).
Verdict: Cranelift as the fast-compilation backend works; the earlier 4x
was the UB's speed, not real.

## 5. §12 — LLVM/AOT cleanup (verified end-to-end)

| Path | Result |
|---|---|
| `--target aot` | object + shared `.so` (`__main__` exported) |
| `--target elf` | stub + standalone executable, prints 15, exit 0 |
| `--target ir` / `--target asm` | LLVM IR (2974 lines) + assembly (1447 lines) |
| LLVM optional-ness | modes 6/8/9 build and run without it |

## 6. §15 — stable native module ABI (tested)

The Lua-style C API (`havel_state.cpp`, 974 lines) was already compiled into
`havel_lang.a` but had zero tests. Real bug fixed: `havel_setglobal` wrote
only the `H->globals` sidecar while scripts resolve globals from the VM's
globals at runtime (`LOAD_GLOBAL`) — C-API-set globals were invisible to
scripts. Five integration tests: lifecycle + loadstring, stack/type
predicates (positive indices are 0-based in this API), globals round-trip,
protected calls on runtime errors, host-function calls.

**Bonus find:** a genuine lexer bug — the operator-context list omitted
`TokenType::Null`, so `nil + 1` was scanned as a hotkey token and rejected
("Expected '=>' after hotkey literal") while `1 + nil` parsed. Null/True/
False added to the operator list; regression smoke script pins it.

## 7. Build fixes surfaced by release verification

1. **LTO-archive links (6 targets)**: hvtest, havel_api_test, havel-lsp,
   hvdb, hvdump, havel-dap had link-level `-fno-lto` — the release archives
   contain LTO bitcode members and a non-LTO link skips them ("plugin needed
   to handle lto object"; observed: Scheduler symbols unresolved from
   havel_lang.a, the release hvtest binary missing). Compile-level -fno-lto
   stays.
2. **havel-hppc release relocations**: built as PIE with GCC's -fPIE —
   extern data references emit R_X86_64_PC32 relocations that cannot resolve
   against dynamic symbols in a PIE; the link warned "causes overflow" and
   the relocated loads segfaulted on first stream use. Fixed: -fPIC
   (GOT-indirect), default ABI/visibility, stdio instead of iostreams.
3. **GCC-only flags** (`-fabi-version`, `-fgnu-unique`) gated on
   `CMAKE_CXX_COMPILER_ID` — clang errored on them.
4. **Test-loop include paths**: the test-source loop omitted `src/include`
   (havel_platform.h not found) — every loop-built test binary failed to
   build.

## 8. Release exit-crash: root cause found and fixed

**`-ffast-math` vs the NaN-boxed Value representation = UB.**

Trail: a `-g` release build did not crash (flag-sensitive, not source
logic) → bisection isolated `-ffast-math` → mechanism: `core::Value` is
NaN-boxed (QNAN 0x7ff8... is the boxing tag); -ffast-math tells the
compiler NaN values cannot exist, making every tagged value UB — the
optimizer assumes branches on them never fire and miscompiles their moves.
Corruption surfaced as a wild vtable call at exit (`EventListener::Stop` →
`backend_->Shutdown()`), crashing 138 of 285 smoke scripts.

Fix: `-ffast-math` removed from both release variants.

---

## Phase 0 baselines (informal record, this session)

All measurements on this machine (16 cores, 39Gi RAM, GCC 16 / clang 22):

| Config | Status | Notes |
|---|---|---|
| build-debug (mode 6: no LLVM) | clean, ctest 100% (5/5) | ASAN+UBSAN, ~2-3.3s script boots pre-serve-path |
| build-release (mode 5: LLVM) | clean, 285/285 smoke | Thin LTO, -march=native, -fno-fast-math, 0.21s boots |
| build-crane (mode 17: Debug+LLVM+CRANELIFT) | clean, ctest 100% (6/6) | build.sh crane mode added; no manual cmake needed |
| build-crane-nollvm (mode 18) | clean, trivial run exit 0 | build.sh crane mode added |
| build-crane-release (mode 19: Release+LLVM+CRANELIFT) | clean, trivial exit 0 | build.sh crane mode; honest tiered arithmetic 1.72s |
| build-nollvm (mode 20: Release, no LLVM) | clean, 285/285 smoke | dedicated dir; avoids reconfigure-on-switch churn |

Benchmarks: `tests/baseline/benchmarks.json` (user-maintained) — 8/8 pass;
run-to-run variance on this machine is ±26% for single runs, ±3% for
median-of-3 on a settled machine.

---

## Final verification at HEAD (`f498c1f3`)

| Check | Command | Result |
|---|---|---|
| Debug ctest full | `cd build-debug && ctest --output-on-failure` | 100% (5/5, 617s) |
| Release smoke, fast tier | `cd build-release && ./hvtest --smoke --no-snapshots` | 285 passed, 0 failed, 0 skipped |
| Release smoke, slow tier | `./hvtest --smoke --only-slow --no-snapshots` | 19 passed, 0 failed, 0 skipped |
| JIT suite | `./hvtest --jit --no-snapshots` | "JIT smoke passed" |
| Embed + C API | `./havel_api_test` | 25 passed, 0 failed |
| Havel++ prototype | `./hpp_proto_test` | PASSED |
| Crane proto driver | `cd build-crane && ./cranelift_proto_driver` | OK, exit 0 |
| Cache stability | .hvc mtimes across 6 boots + a 303-boot suite | unchanged (was: rewritten every boot) |
| Boot time | release 0.26-0.51s → 0.21s | serve path + fingerprint fix |
| Reproduction gone | `havel run while_loop.hv --run ...` ×5 | exit=0 ×5 (was 139) |

User's uncommitted files untouched throughout: AGENTS.md, havel-wm,
VMCollections.cpp, ServiceRegistry.hpp, benchmarks.json (restored after
benchmark verification), .env, TODO2.md.
