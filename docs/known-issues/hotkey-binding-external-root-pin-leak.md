# A discarded hotkey-binding pin leaked one GC external root per binding for the process lifetime

**Status:** FIXED three times over. (1) Shutdown drain: each hotkey registration
pins its hotkey-context object with `VM::pinExternalRoot`; the pin handle
was discarded, so roots stayed live until process exit. `trackExternalRoot`
hands the handle to the VM shutdown drain
(`src/havel-lang/compiler/vm/VM.cpp`, `tracked_external_roots_`).
(2) Mid-session release: `hotkey.remove()` now releases ALL registration
pins - the action callback, the condition callback, and the context-object
root (HotkeyModule::removeById releases via `vm.releaseCallback` and
`vm.unpinExternalRoot`; InputBridge stores the root handle per binding via
`HotkeyModule::setContextRootId`). Before (2), a register/remove cycle
leaked 3 roots per cycle for the process lifetime - measured 180 roots
after 60 cycles by `scripts/tests/test_hotkey_root_lifetime.hv` (now the
regression lock). `gc_external_root_count()` (new, read-only) exposes the
live pin count for this class of test.
(3) Callback-pin drain and bulk removal (Nov 9, the defect this file's
audit table wrongly called covered). Two further leaks survived (1) and
(2):

- **Callback pins had no shutdown drain.** `VM::registerCallback` pins an
  external root and returns that root id AS the `CallbackId`
  (`src/havel-lang/compiler/vm/VMCb.cpp`). `~VM` released
  `host_function_gc_roots_`, `module_cache_gc_roots_` and
  `tracked_external_roots_` - but NOT the callback pins. A binding still
  live at shutdown (a hotkey action closure, a hotkey condition closure, an
  event-listener callback) therefore kept its pin for the whole process.
  Measured: 3 hotkeys -> 6 pins live, 3 still pinned at shutdown (the
  context objects were drained; the callback pins were not); 130-hotkey
  config -> **263 roots still pinned at VM shutdown**.
  Fix: `VM::registerCallback` records the id in `callback_root_ids_`,
  `VM::releaseCallback` erases it, and `~VM` drains the set.

- **`removeAll()` / `clearAll()` dropped the binding map without releasing
  the pins.** They cleared `g_hotkeyContexts` and released nothing, so the
  action/condition/context pins of every removed binding leaked for the
  whole process - observable MID-SESSION, not just at shutdown.
  Measured: 2 hotkeys -> 4 pins still live after `removeAll()` + GC.
  Fix: both methods release the same three handles per binding before
  erasing, inline rather than through `removeById` (which re-takes
  `g_hotkeyContextsMutex`, which is not recursive) - commit `5b1e8f2c6`.

Regression lock: `scripts/smoke/test_issue_hotkey_pin_lifetime.hv`
(mid-session pin accounting for removeAll/remove/conditional-remove, plus
a CHILD process that registers bindings and exits - the shutdown drain is
only observable from outside the process, so the child asserts that no
"external roots still pinned" warning is logged).

### Verification on the current tree (Nov 12 2026)

Binary: `/host/havel/build-debug/havel`, self-reporting
`commit 3ef0fa46ca63 (main, dirty)` = HEAD `3ef0fa46c`.

The SHUTDOWN drain, measured directly (the child must exit gracefully,
otherwise ~VM never runs and the assertion is vacuous):

    env -u DISPLAY -u WAYLAND_DISPLAY -u XAUTHORITY -u XDG_RUNTIME_DIR \
      HAVEL_HEADLESS=1 /host/havel/build-debug/havel <3-live-bindings.hv>
    -> exit 0
    -> [profiler] profiler: ... (the destructor ran)
    -> "external roots still pinned" count: 0

A SIGTERM run of the same script prints NO warning but proves nothing -
the signal path skips the VM destructor, so the drain never executes. The
drain assertion must observe a GRACEFUL exit (the profiler dump and the
exit code are the evidence that the destructor ran).

The USER's own script (`~/scripts/havel/hotkeys0.3.hv`, 130-hotkey config)
went from **263** "external roots still pinned at VM shutdown" (the
pre-fix binary in the Nov 8 log) to **0**.

### Box contention caveat for the gates

The gate runs on this box were dominated by another session's 12-process
CPU-parallel job (load average 50+ on 14 CPUs), not by the code under
test:

- gate: `smoke: 361 passed, 29 failed, 0 skipped | 390 files` - every
  failure a `timeout`, and the failing SET changed between runs.
- the same 29 re-run standalone: the same timeouts.
- the same 8 re-run with `--timeout 600`: still timeouts.
- ONE trivial test (`arithmetic_add`, ~4 s on a quiet box) took **611 s**
  standalone under the load.
- jit-parity: 9 of 10 scripts failed with "interpreted run hung" - the
  INTERPRETED (non-JIT) leg hung, which cannot be a JIT defect; the one
  script that did run passed with `tier1=4` (tiering exercised).

These results were UNVERIFIED, not failures. The clean gate + jit-parity
run was completed later - see below.

### Clean gate + jit-parity (Nov 13 2026) - the gates are now GREEN

Binary: `/host/havel/build-debug/havel`, self-reporting
`commit 93ffea795793 (main, dirty)` = HEAD `93ffea7957` (the tree that
carries `93ffea795` "tests: make the shutdown-drain assertion
non-vacuous under load").

Rebuild: `./build.sh 6 build` -> `[ok] build ok in 1190s`, rc=0.
`./build.sh status` reports `build-debug` at `93ffea7957`, STALE-MOD 0
(no stale module plugins).

SMOKE GATE - the canonical command, on the DEFAULT bytecode cache, with
2 workers (`HAVEL_TEST_THREADS=2`) so the box's other sessions get CPU:

    HAVEL_TEST_THREADS=2 ./build.sh queue test --kind gate
    -> smoke: 390 passed, 0 failed, 0 skipped | 390 files, 8.38766e+06ms
       tests, 21506.8ms avg
    -> [FAIL] lines: 0
    -> exit 0

The gate took ~70 min wall-clock because the box was shared (slowest
startup `test_hpkg_toolchain_store=139012ms`); every test still
completed inside its budget - nothing timed out, nothing was skipped.

JIT-PARITY GATE - against a cranelift-enabled build of the same tree
(`/host/havel-3/build-crane-nollvm/havel`, `ENABLE_CRANELIFT:BOOL=ON`,
commit `253a3a5ad04e`; `git diff --stat 253a3a5ad04e HEAD -- src/` is
EMPTY, so its `src/` is byte-identical to main HEAD, and the pin-leak
fixes `9176128fc` / `5b1e8f2c6` / `560c6b409` are all its ancestors),
with a PRIVATE cache:

    XDG_CACHE_HOME=<scratch> HAVEL_BIN=/host/havel-3/build-crane-nollvm/havel \
      TMO=300 scripts/ci/jit-parity-test.sh
    -> PASS for_loop_range (tier1=4)
    -> PASS for_loop_array (tier1=4)
    -> PASS for_loop_break (tier1=4)
    -> PASS loop_with_break (tier1=4)
    -> PASS do_while (tier1=4)
    -> PASS closure_loop_capture (tier1=5)
    -> PASS chunk_navigator (tier1=8)
    -> PASS test_sorted_bare_global (tier1=6)
    -> PASS test_issue_jit_string_ordering (tier1=9)
    -> PASS selfhost (tier1=67)
    -> jit-parity: 10 passed, 0 failed
    -> exit 0

Every script ACTUALLY TIERED (tier1 = 4..67), so the gate is not
reporting an interpreted run as a JIT pass.

The private cache is required for the parity gate (the shared
`~/.cache/havel` makes every launch recompile the self-hosted compiler
first - measured 401 s timeout on the shared cache vs 57 s rc=0 on a
private one). The private cache trips the PRE-EXISTING CPython leak
(`docs/known-issues/python-bridge-cpython-module-leak.md`) in the SMOKE
gate, so the smoke gate above uses the default cache instead: a
private-cache smoke run gave `smoke: 389 passed, 1 failed` where the one
failure was `test_python_bridge` with
`SUMMARY: AddressSanitizer: 2289 byte(s) leaked in 3 allocation(s)` -
the documented leak, reproduced identically on a binary that carries
none of the cache-key work, and PASSING on the default cache
(`./build-debug/hvtest --smoke --timeout 300 test_python_bridge` ->
`smoke: 1 passed, 0 failed`, 20.2 s).


## Symptom

Progressive scheduler slowdown in long-lived hotkey configs: every GC cycle
marks all pinned roots, so cost grows with total bindings ever registered
(318 for a large config), not with live ones. Observable pre-fix only via
the shutdown warning (`[VM][GC] N external roots still pinned at VM
shutdown`) or heap introspection.

## Mechanism

`src/host/module/bridges/InputBridge.cpp` (both `handleHotkeyRegister` and
`handleHotkeyRegisterConditional`): `pinExternalRoot` returns the only
handle that can release the root; the original code dropped it. The
comment at those sites documents this exactly - and that documentation
existed BEFORE the fix, as a post-mortem of the diagnosis that was never
converted into a tracked defect or test. That gap is what
`AGENTS.md` "Defect Discovery and Session Handoffs" now prohibits.

## Reproducer (manual-run; no input injection)

```havel
// scripts/tests/test_hotkey_root_lifetime.hv (committed)
_base = gc_external_root_count()
// register 60 conditional bindings, then:
for _n in _names { hotkey.remove(_n) }
gc_collect_full()
gc_collect()
if gc_external_root_count() > _base { print("FAIL: roots leaked"); exit(1) }
```

## Audit of the same pattern (Oct 26, havel-2 session)

Every `pinExternalRoot` call site was traced to a release path:

| Site | Release |
|---|---|
| InputBridge.cpp hotkey ctx (both register paths) | `trackExternalRoot` → shutdown drain AND `hotkey.remove()` → `setContextRootId`-stored handle released in `removeById` AND `removeAll()`/`clearAll()` → `releaseBindingPins()` |
| VMCb.cpp (`registerCallback`) | `releaseCallback` → `unpinExternalRoot`, plus the `callback_root_ids_` shutdown drain for bindings still live at exit (added Nov 9 - the table previously claimed this site was fully covered, and it was not: `releaseCallback` only runs when the binding is removed mid-session) |
| VMCollections.cpp:29 | RAII `PinnedRootGuard` |
| VM.cpp:5531 (module closure wrappers) | `host_function_gc_roots_` → shutdown drain |
| VM.cpp:5794/5835/6912/7365 (transient copies/exports) | immediate `unpin` after use |
| JitRuntimeBridges.cpp:2283+ (array HOF bridges) | paired `unpin` |

Timer/interval pins (GC.hpp:498, ConcurrencyBridge.cpp:16, VM.cpp:149/214)
are released via sweep-freed hooks (`addIntervalFreedHook`/
`addTimeoutFreedHook`) when the sweep collects a dropped timer - those
comments describe FIXED machinery, not live hazards. C++-level root
regression: `host-root-lifetime` in `src/hvtest/smoke_runner.cpp` and
`src/havel-lang/tools/BytecodeSmokeMain.cpp`.

Remaining suspicious-comment triage from the codebase grep
(`this leaks|never released|not released|memory leak`): all other hits
are fixed-defect post-mortems ("used to leak", "otherwise leaks") or
leak-detector infrastructure. No additional live leak was found in the
pin/unpin surface this pass.
