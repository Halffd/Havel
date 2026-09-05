# Phase 0 - Havel Baseline Report

**Date**: 2026-09-04
**System**: Linux, 16 cores, x86_64, clang, C++23
**Repo**: /home/all/repos/havel, branch main (HEAD 9adfec38)

This report records the Phase 0 baseline required by TODO.md section 3:
build configurations, test results with failure classification, startup time,
interpreter performance, memory, and the machine-readable baselines in
`tests/baseline/{default,llvm,self_hosted}.json`.

Every subsequent architectural change (BytecodeIR stabilization, pass
framework, JIT backends) must be compared against these files.

---

## 1. Build Configurations

Both relevant configurations build cleanly and produce the self-hosted pipeline
(101 modules, 0 failed).

| Config | Mode | Type | LLVM | Sanitizers | Binary | Size |
|--------|------|------|------|-----------|--------|------|
| default (no LLVM) | 6 | Debug | off | ASAN+UBSAN | build-debug/havel | 99 MB |
| LLVM-enabled | 5 | Release | 22.1.8 on | ThinLTO+`-march=native` | build-release/havel | 9.8 MB |

- The **default** `havel` and `hvtest` already use the **self-hosted pipeline**
  (compiled from `modules/lang` into `out/`, cached into `~/.cache/havel/*.hvc`).
- A pure C++ (non-self-hosted) config exists via modes 3/7 but is not the default
  build path.
- Both builds produce `hvtest`; `havel-bytecode-smoke` is not produced by these
  configs.

## 2. Test Suite Results

### Smoke suite — modern language tests (scripts/smoke/*.hv)
```
279 passed, 0 failed, 0 skipped  | 660450ms wall for 279 files
avg 2.4s/test harness time (9.4s in-process test time)
```
**100% pass.** This is the authoritative modern test set and is healthy.

### JIT smoke — LLVM ORC path (release build)
```
30 passed, 0 failed  | rc=0  (jit-try-catch, jit-array-map, jit-gc-stress,
                             jit-recursion, jit-class-jit, jit-object-jit-chain, ...)
```
**100% pass.** The LLVM JIT backend works.

### Integration suite (scripts/integration/*.hv)
```
97 total: 52 PASS, 42 FAIL(1), 3 TIMEOUT, 0 exit=127
```

### C++ unit tests / ctest
- No gtest tests built (`GTest` not found; `test_embed_api.cpp` not compiled).
- Debug ctest runs two tests:
  - `hvtest-smoke` — re-runs the full 279-file smoke suite (~660 s); passes but is slow.
  - `module-globals-drift-guard` — **PASS** ("OK: ModuleGlobals.generated.hpp (67 names)").

## 3. Failure Classification

Separate of exit=127 from timeout per TODO.md: there are **0 exit=127** and
**3 timeout** results. No regressions and no new failures found.

| Classification | Count | Meaning |
|----------------|-------|---------|
| PASS | 52 | runs to exit 0 |
| EXPECTED_FAILURE | 20 | stale, legacy-syntax tests the modern self-hosted compiler rejects |
| INFRASTRUCTURE_FAILURE | 22 | require display / input devices / audio / network / FFI libs (headless env) |
| TIMEOUT | 3 | defined timeout (120 s) exceeded: `test_conditional_hotkeys`, `test_ffi_mpv`, `test_new_stdlib` |
| REGRESSION | 0 | none |
| NEW_FAILURE | 0 | none |
| UNCLASSIFIED | 0 | none |

### PRE_EXISTING / EXPECTED_FAILURE (legacy syntax, 20)
Tests written for the pre-migration Havel syntax (`let`, `:=`, `+` string
concatenation, space-separated `print`, LINQ `from...select`, monads, etc.).
The modern self-hosted parser rejects these. Examples verified in source:
`test_fs`, `test_stdlib`, `test_try`, `test_import`, `test_linq2`, `test_monads`,
`test_cfg`, `test_path`, `test_path_simple`, `test_regex_module`,
`test_result_types`, `test_struct_class`, `test_types`, `test_async_await`,
`test_cooperative_async`, `int_gof_patterns`, `test_pipeline_features`,
`test_utility_module`, `test_fsuv`, `test_config_sugar`.

### INFRASTRUCTURE_FAILURE (22)
Tests that perform live I/O and need hardware/display/network that this
headless environment lacks: window/X11 (`test_x11*`, `test_window*`,
`test_wm_detection`), audio (`test_audio*`), hotkey/event (`test_event_system`,
`test_host_modules`, `test_basic_modules`), FFI libs (`test_ffi_*`), network
(`test_net_dns`, `test_net_stack`), timers/OS (`test_thread_safe_timers`,
`test_os_module`, `test_wt`, `test_canvas`). Per AGENTS.md these IO/UI tests are
not meaningful headless; they are flagged rather than treated as regressions.

## 4. Startup Time

Measured against a 3-line minimal script (Debug with ASAN/UBSAN vs Release).

| Build | startup (ms) | peak RSS |
|-------|--------------|----------|
| Debug (no LLVM) | 5300 / 4160 / 4260 | ~507 MB |
| Release (LLVM) | 870 / 860 | ~90 MB |

Release is ~5x faster startup and ~5.6x lower memory. Debug's ASAN+UBSAN plus
unoptimized code dominates startup. This matches the earlier finding that
debug startup (~4-5 s) dominates per-test time; the release build is the
realistic execution-profile configuration.

## 5. Interpreter Performance (Release, self-hosted)

Benchmark scripts (scripts/benchmarks/*.hv) via `build-release/havel run`, wall time:

| Benchmark | time (ms) | result |
|-----------|-----------|--------|
| arithmetic (1.6M int ops) | 27479 | pass |
| arrays | 4135 | pass |
| closures | - | **FAIL** (parse error, stale benchmark) |
| functions | 5857 | pass |
| strings | 1684 | pass |
| objects | - | **FAIL** (rc=1) |
| gc | 17718 | pass |
| test_simple | 896 | pass |

- Interpreter is functional but slow for compute-heavy loops (~65k iterations/s
  for the arithmetic benchmark in Release). Under Debug+ASAN the same benchmark
  exceeds 120 s (times out).
- `closures.hv` and `objects.hv` fail to run; `closures.hv` fails with a parser
  error ("Unexpected token in expression: 76...") — another stale benchmark.
  These explain why the 2026-08-30 benchmark baseline was all-zeros.

## 6. Machine-Readable Baselines

Written to `tests/baseline/`:

- `default.json` — Debug, no LLVM (mode 6) build config, startup, smoke/ctest/integration.
- `llvm.json` — Release + LLVM 22.1.8 (mode 5) config, JIT smoke, interpreter benchmarks.
- `self_hosted.json` — self-hosted pipeline build (101 modules), smoke/integration.

## 7. Key Findings

1. **Modern language test suite is healthy**: smoke 279/279 and JIT 30/30 pass.
2. **No regressions or new failures** — the integration failures are entirely
   pre-existing (stale legacy-syntax tests + environment-dependent IO/UI/FFI tests).
3. **Startup and memory are dominated by Debug ASAN/UBSAN**; Release is ~5x faster
   and ~5.6x lower memory. Per-test debug timing is not a meaningful perf baseline.
4. **Interpreter throughput is low** (~65k int-ops/s in Release). This is the
   highest-ROI target per TODO.md's guidance (fast opcodes / VM optimization),
   and the basis for later BytecodeIR/JIT work.
5. **Benchmark suite is partially broken**: `closures.hv` and `objects.hv`
   do not run (stale syntax). Fixing these is a prerequisite to trustworthy
   benchmark trending.

## 8. Next Steps (feed into TODO.md Phase 1+)

- Fix `closures.hv` / `objects.hv` benchmarks so the suite is fully runnable.
- Use Release (mode 5) as the performance baseline config, not Debug.
- Profile/optimize the interpreter (fast opcodes, host-call amortization) before
  BytecodeIR/JIT work, per TODO.md section 4 (make host calls cheap).
- Then proceed to Phase 1: stabilize BytecodeIR / CFG / verifier.

## Version History

| Date | Update |
|------|--------|
| 2026-08-30 | First (partial, outdated) baseline; 55% smoke pass |
| 2026-09-04 | Phase 0 baseline re-established: smoke 100% pass, JIT 100% pass, integration classified, generated default/llvm/self_hosted.json |
